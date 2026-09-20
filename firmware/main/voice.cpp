#include "voice.h"
#include "audio.h"
#include "bitbot.h"
#include "camera.h"
#include "display.h"
#include "xiaozhi.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <esp_opus_dec.h>
#include <esp_opus_enc.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <model_path.h>
#include <memory>
#include <mutex>
#include <vector>

// Protocol: 78/xiaozhi-esp32 docs/websocket.md, binary protocol version 1 (raw Opus frames).
namespace bitbot {
static constexpr const char* kTag = "bitbot_voice";
static constexpr int kFrameMs = 60, kFrameSamples = kAudioRate * kFrameMs / 1000;  // Opus frame, both ways
// Wake phrases for the English MultiNet (mn7_en), given as esp-sr phonemes (its own alphabet, see
// model/multinet_model/fst/commands_en.txt and tool/multinet_g2p.py).
//
// Measured on this board by feeding recorded clips straight into the recogniser: it only matches
// phrases built from real English words. "hey robot" (p=0.33) and "hey bit robot" (p=0.26) are
// recognised; "hey bitbot", "hey bit bot" and even "hey bot" never are, however clearly spoken.
// A true "hey bitbot" needs a WakeNet model trained on that phrase; see docs/next-steps.md.
// ponytail: the button always works, so treat these as a convenience, not the only way in.
static const struct { const char* text; const char* phonemes; } kWakePhrases[] = {
    {"hey bit robot", "hd BgT RbBnT"},  // closest to "BitBot" that the model can hear
    {"hey robot", "hd RbBnT"},
    {"okay robot", "bKd RbBnT"},
    {"hi robot", "hi RbBnT"},
};
// esp_websocket_client aborts the whole connection when a write times out, so this is the longest
// Wi-Fi stall a conversation survives, not a per-frame deadline. Blocking here drops some mic DMA;
// losing a little audio beats losing the call.
static constexpr int kSendTimeoutMs = 3000;
static constexpr float kWakeThreshold = 0.2f;  // ponytail: esp-sr default; lower made detection worse, not better

enum class Voice { Off, Idle, Connecting, Listening, Speaking };
static std::atomic<Voice> voice{Voice::Off};
static std::atomic<bool> wake_heard{false}, button_pressed{false}, tts_done{false};
static std::atomic<int> pending_audio{0};
static std::atomic<int64_t> last_activity{0};
static std::atomic<int> sent_frames{0}, received_frames{0}, mic_peak{0}, played_samples{0}, decode_errors{0}, underruns{0};  // for the once-a-second log line

static std::mutex ws_mutex;  // guards ws and session_id between the voice, mic and WebSocket tasks
static esp_websocket_client_handle_t ws = nullptr;
static std::string session_id;
static std::string vision_url, vision_token;  // from the MCP handshake; token is a secret
static EventGroupHandle_t ws_events;
static constexpr EventBits_t kWsConnected = 1, kWsHello = 2, kWsClosed = 4;
struct Packet { uint8_t* data; size_t size; };
static QueueHandle_t playback;  // Opus packets from the server, freed by the speaker task

struct Prefs { int volume = 70, idle_s = 30; bool captions = true; };
static Prefs ReadPrefs() {
    std::lock_guard<std::mutex> lock(shared.mutex);
    const auto* s = cJSON_GetObjectItemCaseSensitive(shared.document, "settings");
    Prefs p;
    if (const auto* v = cJSON_GetObjectItemCaseSensitive(s, "volume"); cJSON_IsNumber(v)) p.volume = v->valueint;
    if (const auto* v = cJSON_GetObjectItemCaseSensitive(s, "idleSeconds"); cJSON_IsNumber(v)) p.idle_s = std::max(10, v->valueint);
    if (const auto* v = cJSON_GetObjectItemCaseSensitive(s, "captions"); cJSON_IsBool(v)) p.captions = cJSON_IsTrue(v);
    return p;
}
static Prefs prefs;

// XiaoZhi's LLM emotions onto the face's expressions.
static Expression FromEmotion(const std::string& e) {
    if (e == "happy" || e == "cool" || e == "confident" || e == "relaxed") return Expression::Happy;
    if (e == "laughing" || e == "funny") return Expression::Laughing;
    if (e == "loving" || e == "kissy") return Expression::Love;
    if (e == "surprised" || e == "shocked") return Expression::Surprised;
    if (e == "angry") return Expression::Angry;
    if (e == "sad" || e == "crying") return Expression::PutOff;
    if (e == "embarrassed" || e == "delicious") return Expression::Cute;
    if (e == "winking" || e == "silly") return Expression::Winking;
    if (e == "thinking" || e == "confused") return Expression::Sceptical;
    if (e == "sleepy") return Expression::Exasperated;
    return Expression::Neutral;
}
static void Caption(const std::string& text, bool is_caption = true) {
    if (is_caption && !prefs.captions) { SetDisplayCaption(" "); return; }  // blank, not the idle guide
    SetDisplayCaption(text);
}

static bool SendText(const std::string& text) {
    std::lock_guard<std::mutex> lock(ws_mutex);
    return ws && esp_websocket_client_send_text(ws, text.data(), text.size(), pdMS_TO_TICKS(kSendTimeoutMs)) >= 0;
}
static void SendJson(const char* type, std::initializer_list<std::pair<const char*, const char*>> fields) {
    Json message(cJSON_CreateObject());
    { std::lock_guard<std::mutex> lock(ws_mutex); cJSON_AddStringToObject(message.value, "session_id", session_id.c_str()); }
    cJSON_AddStringToObject(message.value, "type", type);
    for (const auto& [key, value] : fields) cJSON_AddStringToObject(message.value, key, value);
    SendText(Print(message.value));
}

// MCP (docs/mcp-protocol.md): the server discovers the camera through tools/list and then calls it.
// The reply travels as {"type":"mcp","payload":<JSON-RPC 2.0>}.
static void SendMcp(cJSON* payload) {  // takes ownership of payload
    Json message(cJSON_CreateObject());
    { std::lock_guard<std::mutex> lock(ws_mutex); cJSON_AddStringToObject(message.value, "session_id", session_id.c_str()); }
    cJSON_AddStringToObject(message.value, "type", "mcp");
    cJSON_AddItemToObject(message.value, "payload", payload);
    SendText(Print(message.value));
}
static cJSON* McpResult(double id) {
    auto* payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(payload, "id", id);
    return payload;
}
static void McpError(double id, const char* message) {
    auto* payload = McpResult(id);
    auto* error = cJSON_AddObjectToObject(payload, "error");
    cJSON_AddNumberToObject(error, "code", -32603);
    cJSON_AddStringToObject(error, "message", message);
    SendMcp(payload);
}
// Taking and uploading a photo takes seconds, so it runs in its own task.
struct PhotoRequest { double id; std::string question; };
static void PhotoTask(void* argument) {
    std::unique_ptr<PhotoRequest> request(static_cast<PhotoRequest*>(argument));
    std::string url, token;
    { std::lock_guard<std::mutex> lock(ws_mutex); url = vision_url; token = vision_token; }
    const std::string answer = CameraExplain(url, token, request->question);
    if (answer.empty()) McpError(request->id, "The camera could not take or upload a photo");
    else {
        auto* payload = McpResult(request->id);
        auto* result = cJSON_AddObjectToObject(payload, "result");
        auto* content = cJSON_AddArrayToObject(result, "content");
        auto* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "text");
        cJSON_AddStringToObject(item, "text", answer.c_str());
        cJSON_AddItemToArray(content, item);
        cJSON_AddBoolToObject(result, "isError", false);
        SendMcp(payload);
    }
    vTaskDelete(nullptr);
}
static void OnMcp(const cJSON* payload) {
    const std::string method = Text(payload, "method");
    const auto* id_item = cJSON_GetObjectItemCaseSensitive(payload, "id");
    const double id = cJSON_IsNumber(id_item) ? id_item->valuedouble : 0;
    if (method == "initialize") {
        const auto* vision = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(payload, "params"), "capabilities"), "vision");
        {
            std::lock_guard<std::mutex> lock(ws_mutex);
            vision_url = Text(vision, "url"); vision_token = Text(vision, "token");
        }
        ESP_LOGI(kTag, "MCP ready; vision service %s", vision_url.empty() ? "not offered" : "available");
        auto* payload_out = McpResult(id);
        auto* result = cJSON_AddObjectToObject(payload_out, "result");
        cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
        cJSON_AddObjectToObject(cJSON_AddObjectToObject(result, "capabilities"), "tools");
        auto* info = cJSON_AddObjectToObject(result, "serverInfo");
        cJSON_AddStringToObject(info, "name", "bitbot");
        cJSON_AddStringToObject(info, "version", "0.1.0");
        SendMcp(payload_out);
    } else if (method == "tools/list") {
        auto* payload_out = McpResult(id);
        auto* result = cJSON_AddObjectToObject(payload_out, "result");
        auto* tools = cJSON_AddArrayToObject(result, "tools");
        if (CameraReady()) {
            auto* tool = cJSON_CreateObject();
            cJSON_AddStringToObject(tool, "name", "self.camera.take_photo");
            cJSON_AddStringToObject(tool, "description",
                                    "You have a camera. When the user asks what you see, or about anything in front of "
                                    "you, take a photo with this tool and answer from it.\nArgs:\n  `question`: what to "
                                    "look for in the photo.");
            auto* schema = cJSON_AddObjectToObject(tool, "inputSchema");
            cJSON_AddStringToObject(schema, "type", "object");
            auto* properties = cJSON_AddObjectToObject(schema, "properties");
            auto* question = cJSON_AddObjectToObject(properties, "question");
            cJSON_AddStringToObject(question, "type", "string");
            auto* required = cJSON_AddArrayToObject(schema, "required");
            cJSON_AddItemToArray(required, cJSON_CreateString("question"));
            cJSON_AddItemToArray(tools, tool);
        }
        cJSON_AddStringToObject(result, "nextCursor", "");
        SendMcp(payload_out);
    } else if (method == "tools/call") {
        const auto* params = cJSON_GetObjectItemCaseSensitive(payload, "params");
        if (strcmp(Text(params, "name"), "self.camera.take_photo") != 0) { McpError(id, "Unknown tool"); return; }
        auto* request = new PhotoRequest{id, Text(cJSON_GetObjectItemCaseSensitive(params, "arguments"), "question")};
        SetDisplayCaption("Looking ...");
        if (xTaskCreate(PhotoTask, "photo", 8192, request, 4, nullptr) != pdPASS) { delete request; McpError(id, "Busy"); }
    }
}
static void OnJson(const std::string& text) {
    Json message(cJSON_Parse(text.c_str()));
    const std::string type = Text(message.value, "type");
    if (type != "tts" || strcmp(Text(message.value, "state"), "sentence_start") != 0)
        ESP_LOGD(kTag, "Server: %.200s", text.c_str());
    if (type == "hello") {
        { std::lock_guard<std::mutex> lock(ws_mutex); session_id = Text(message.value, "session_id"); }
        xEventGroupSetBits(ws_events, kWsHello);
    } else if (type == "stt") {
        last_activity = NowMs();
        Caption(Text(message.value, "text"));
        SetExpression(Expression::Sceptical);  // thinking about it
    } else if (type == "llm") {
        SetExpression(FromEmotion(Text(message.value, "emotion")));
    } else if (type == "tts") {
        const std::string state = Text(message.value, "state");
        if (state == "start") { tts_done = false; voice = Voice::Speaking; }
        else if (state == "sentence_start") Caption(Text(message.value, "text"));
        else if (state == "stop") tts_done = true;
    } else if (type == "mcp") {
        OnMcp(cJSON_GetObjectItemCaseSensitive(message.value, "payload"));
    } else if (type == "system" && strcmp(Text(message.value, "command"), "reboot") == 0) {
        ESP_LOGW(kTag, "Server asked for a reboot; ignored");  // ponytail: not needed yet
    }
}
static void OnWebsocket(void*, esp_event_base_t, int32_t id, void* event) {
    auto* data = static_cast<esp_websocket_event_data_t*>(event);
    if (id == WEBSOCKET_EVENT_CONNECTED) { xEventGroupSetBits(ws_events, kWsConnected); return; }
    if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_CLOSED || id == WEBSOCKET_EVENT_ERROR) {
        xEventGroupSetBits(ws_events, kWsClosed); return;
    }
    if (id != WEBSOCKET_EVENT_DATA) return;
    // Large messages arrive in pieces (payload_offset/payload_len); put them back together.
    static std::string frame;
    static uint8_t opcode = 0;
    if (data->op_code == 0x1 || data->op_code == 0x2) opcode = data->op_code;
    else if (data->op_code != 0x0) return;  // ping/pong/close are handled by the client
    if (data->payload_offset == 0 && data->op_code != 0x0) frame.clear();
    frame.append(data->data_ptr, data->data_len);
    if (data->payload_offset + data->data_len < data->payload_len || !data->fin) return;
    if (opcode == 0x1) OnJson(frame);
    else if (voice == Voice::Speaking && frame.size() < 4096) {  // audio while listening is dropped
        ++received_frames;
        Packet packet{static_cast<uint8_t*>(malloc(frame.size())), frame.size()};
        if (!packet.data) return;
        memcpy(packet.data, frame.data(), frame.size());
        ++pending_audio;
        if (xQueueSend(playback, &packet, 0) != pdTRUE) { free(packet.data); --pending_audio; }
    }
    frame.clear();
}

static void CloseSession(const char* why) {
    ESP_LOGI(kTag, "Conversation ended: %s", why);
    esp_websocket_client_handle_t old;
    { std::lock_guard<std::mutex> lock(ws_mutex); old = ws; ws = nullptr; session_id.clear(); }
    if (old) { esp_websocket_client_close(old, pdMS_TO_TICKS(1000)); esp_websocket_client_destroy(old); }
    Packet packet;
    while (xQueueReceive(playback, &packet, 0) == pdTRUE) { free(packet.data); --pending_audio; }
    SetExpression(Expression::Neutral);
    SetDisplayCaption("");
    voice = Voice::Idle;
}
// A short rising two-tone chirp: "I heard you". Only called while nothing else plays.
static void Chirp() {
    static int16_t tone[kAudioRate / 10];
    for (int i = 0; i < static_cast<int>(std::size(tone)); ++i) {
        float f = i < static_cast<int>(std::size(tone)) / 2 ? 880 : 1320, t = static_cast<float>(i) / kAudioRate;
        tone[i] = static_cast<int16_t>(6000 * sinf(2 * static_cast<float>(M_PI) * f * t));
    }
    WriteSpeaker(tone, std::size(tone));
}
static void OpenSession(const char* wake_phrase) {
    voice = Voice::Connecting;
    prefs = ReadPrefs();
    SetVolume(prefs.volume);
    SetExpression(Expression::Surprised);
    SetDisplayCaption("Connecting ...");
    std::string url, token;
    if (!GetXiaozhiWebsocket(url, token)) { CloseSession("not paired"); return; }
    const std::string headers = "Authorization: Bearer " + token + "\r\nProtocol-Version: 1\r\nDevice-Id: " +
                                XiaozhiDeviceId() + "\r\nClient-Id: " + XiaozhiClientId() + "\r\n";
    esp_websocket_client_config_t config = {};
    config.uri = url.c_str(); config.headers = headers.c_str();
    config.crt_bundle_attach = esp_crt_bundle_attach;  // TLS verified against the IDF CA bundle
    config.disable_auto_reconnect = true;
    config.buffer_size = 2048; config.task_stack = 6144; config.network_timeout_ms = 20000;  // first TLS handshake to this server measured ~7 s
    xEventGroupClearBits(ws_events, kWsConnected | kWsHello | kWsClosed);
    auto* client = esp_websocket_client_init(&config);
    if (!client) { CloseSession("no memory for WebSocket"); return; }
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, OnWebsocket, nullptr);
    { std::lock_guard<std::mutex> lock(ws_mutex); ws = client; }
    if (esp_websocket_client_start(client) != ESP_OK ||
        !(xEventGroupWaitBits(ws_events, kWsConnected | kWsClosed, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000)) & kWsConnected)) {
        CloseSession("could not connect"); return;
    }
    SendText(R"({"type":"hello","version":1,"features":{"mcp":true},"transport":"websocket","audio_params":{"format":"opus","sample_rate":16000,"channels":1,"frame_duration":60}})");
    if (!(xEventGroupWaitBits(ws_events, kWsHello | kWsClosed, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000)) & kWsHello)) {
        CloseSession("no hello from server"); return;
    }
    if (wake_phrase) SendJson("listen", {{"state", "detect"}, {"text", wake_phrase}});
    SendJson("listen", {{"state", "start"}, {"mode", "auto"}});  // server-side VAD decides when we stop talking
    SetExpression(Expression::Neutral);
    SetDisplayCaption(" ");  // blank: a clear screen means "go ahead, I am listening"
    last_activity = NowMs();
    voice = Voice::Listening;
    ESP_LOGI(kTag, "Listening (%s)", wake_phrase ? "wake phrase" : "button");
}

static void VoiceTask(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        State net;
        { std::lock_guard<std::mutex> lock(shared.mutex); net = shared.state; }
        std::string url, token;
        const bool available = net == State::Idle && GetXiaozhiWebsocket(url, token);
        const Voice now = voice.load();
        if (now == Voice::Off) {
            if (available) { voice = Voice::Idle; ESP_LOGI(kTag, "Ready: say \"hey robot\" or press the button"); }
            button_pressed = wake_heard = false;
            continue;
        }
        if (now == Voice::Idle) {
            if (!available) { voice = Voice::Off; continue; }
            const bool woke = wake_heard.exchange(false), pressed = button_pressed.exchange(false);
            if (woke || pressed) { Chirp(); OpenSession(woke ? "hey robot" : nullptr); }
            continue;
        }
        // In a conversation. Once a second: what went up and down, and how loud the mic was.
        static int64_t log_at = 0;
        if (NowMs() >= log_at) {
            log_at = NowMs() + 1000;
            int peak = mic_peak.exchange(0);
            ESP_LOGI(kTag, "state %d, sent %d, received %d Opus frames, played %d ms, gaps %d, mic peak %d dBFS",
                     static_cast<int>(now), sent_frames.exchange(0), received_frames.exchange(0),
                     played_samples.exchange(0) * 1000 / kAudioRate, underruns.load(),
                     peak ? static_cast<int>(20 * log10f(peak / 32768.0f)) : -99);
        }
        if (button_pressed.exchange(false)) { SendJson("abort", {{"reason", "user"}}); CloseSession("button"); continue; }
        // Interrupting: the wake phrase during a reply stops the reply and listens again.
        if (now == Voice::Speaking && wake_heard.exchange(false)) {
            SendJson("abort", {{"reason", "wake_word_detected"}});
            Packet packet;
            while (xQueueReceive(playback, &packet, 0) == pdTRUE) { free(packet.data); --pending_audio; }
            tts_done = false;
            SendJson("listen", {{"state", "start"}, {"mode", "auto"}});
            SetExpression(Expression::Surprised);
            SetDisplayCaption(" ");
            last_activity = NowMs();
            voice = Voice::Listening;
            continue;
        }
        if (!available) { CloseSession("offline"); continue; }
        if (xEventGroupGetBits(ws_events) & kWsClosed) { CloseSession("server closed the connection"); continue; }
        if (now == Voice::Speaking && tts_done && pending_audio == 0) {
            vTaskDelay(pdMS_TO_TICKS(250));  // let the last DMA buffers play out before the mic listens again
            tts_done = false;
            SendJson("listen", {{"state", "start"}, {"mode", "auto"}});
            SetExpression(Expression::Neutral);
            last_activity = NowMs();
            voice = Voice::Listening;
        }
        if (now == Voice::Listening && NowMs() - last_activity > prefs.idle_s * 1000LL) CloseSession("quiet for a while");
    }
}

// One task owns the microphone: the wake phrase while idle or speaking, the Opus uplink while
// listening. ponytail: esp-sr's AFE front end sits in front of this in XiaoZhi, but with one mic and
// no echo reference it stopped MultiNet from recognising anything at all, so the mic feeds itdirectly.
static void MicTask(void*) {
    esp_mn_iface_t* multinet = nullptr;
    model_iface_data_t* model = nullptr;
    if (srmodel_list_t* models = esp_srmodel_init("model")) {
        if (char* name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH)) {
            multinet = esp_mn_handle_from_name(name);
            model = multinet->create(name, 3000);
            multinet->set_det_threshold(model, kWakeThreshold);
            esp_mn_commands_clear();
            for (int i = 0; i < static_cast<int>(std::size(kWakePhrases)); ++i)
                esp_mn_commands_phoneme_add(i + 1, kWakePhrases[i].text, kWakePhrases[i].phonemes);
            if (esp_mn_error_t* errors = esp_mn_commands_update(); errors && errors->num > 0)
                ESP_LOGW(kTag, "%d wake phrase(s) rejected by the model", errors->num);
            multinet->print_active_speech_commands(model);
            ESP_LOGI(kTag, "Wake phrases active (%s)", name);
        }
    }
    if (!model) ESP_LOGW(kTag, "No English MultiNet in the model partition: wake phrases off, use the button");
    const int mn_chunk = model ? multinet->get_samp_chunksize(model) : 512;

    esp_opus_enc_config_t enc_config = ESP_OPUS_ENC_CONFIG_DEFAULT();
    enc_config.sample_rate = kAudioRate; enc_config.channel = 1;
    enc_config.bitrate = ESP_OPUS_BITRATE_AUTO;
    enc_config.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    void* encoder = nullptr;
    esp_opus_enc_open(&enc_config, sizeof(enc_config), &encoder);
    int in_size = 0, out_size = 0;
    esp_opus_enc_get_frame_size(encoder, &in_size, &out_size);
    std::vector<uint8_t> opus(std::max(out_size, 512));

    std::vector<int16_t> block(320), wake_buffer, frame;  // 20 ms per read
    wake_buffer.reserve(mn_chunk * 2); frame.reserve(kFrameSamples);
    Voice previous = Voice::Off;
    for (;;) {
        if (!ReadMic(block.data(), block.size())) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        const int16_t* clean = block.data();
        const int samples = block.size();
        for (int i = 0; i < samples; ++i) mic_peak = std::max<int>(mic_peak, std::abs(clean[i]));
        const Voice now = voice.load();
        if (now != previous) { wake_buffer.clear(); frame.clear(); if (model) multinet->clean(model); previous = now; }
        if ((now == Voice::Idle || now == Voice::Speaking) && model) {
            wake_buffer.insert(wake_buffer.end(), clean, clean + samples);
            while (static_cast<int>(wake_buffer.size()) >= mn_chunk) {
                esp_mn_state_t state = multinet->detect(model, wake_buffer.data());
                if (state == ESP_MN_STATE_DETECTED) {
                    const auto* detection = multinet->get_results(model);
                    ESP_LOGI(kTag, "Wake phrase \"%s\" (p=%.2f)", detection->string, detection->num ? detection->prob[0] : 0.0f);
                    wake_heard = true;
                }
                // MultiNet stops listening after its 3 s window unless it is reset.
                if (state != ESP_MN_STATE_DETECTING) multinet->clean(model);
                wake_buffer.erase(wake_buffer.begin(), wake_buffer.begin() + mn_chunk);
            }
        } else if (now == Voice::Listening && encoder) {
            frame.insert(frame.end(), clean, clean + samples);
            while (static_cast<int>(frame.size()) >= kFrameSamples) {
                esp_audio_enc_in_frame_t in = {reinterpret_cast<uint8_t*>(frame.data()), static_cast<uint32_t>(kFrameSamples * 2)};
                esp_audio_enc_out_frame_t out = {opus.data(), static_cast<uint32_t>(opus.size()), 0, 0};
                if (esp_opus_enc_process(encoder, &in, &out) == ESP_AUDIO_ERR_OK && out.encoded_bytes > 0) {
                    std::lock_guard<std::mutex> lock(ws_mutex);
                    if (ws && esp_websocket_client_send_bin(ws, reinterpret_cast<const char*>(opus.data()), out.encoded_bytes, pdMS_TO_TICKS(kSendTimeoutMs)) > 0) ++sent_frames;
                }
                frame.erase(frame.begin(), frame.begin() + kFrameSamples);
            }
        }
    }
}

// Speaker: decode the server's Opus (24 kHz) straight to our 16 kHz; libopus resamples internally.
static void SpeakerTask(void*) {
    esp_opus_dec_cfg_t config = ESP_OPUS_DEC_CONFIG_DEFAULT();
    config.sample_rate = kAudioRate; config.channel = 1;
    void* decoder = nullptr;
    if (esp_opus_dec_open(&config, sizeof(config), &decoder) != ESP_AUDIO_ERR_OK) ESP_LOGE(kTag, "Opus decoder failed");
    std::vector<uint8_t> pcm(kAudioRate * 2 * 120 / 1000);  // room for 120 ms
    // Network audio arrives in bursts. Playing each packet the moment it lands leaves gaps in the
    // I2S stream, which come out as crackle, so a few packets are collected before the speaker starts.
    static constexpr int kPrebufferPackets = 5;  // 5 x 60 ms = 300 ms; ponytail: raise if it still breaks up
    bool playing = false;
    for (;;) {
        if (!playing) {
            if (uxQueueMessagesWaiting(playback) < kPrebufferPackets && !tts_done) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
            playing = true;
        }
        Packet packet;
        if (xQueueReceive(playback, &packet, pdMS_TO_TICKS(200)) != pdTRUE) {
            // Ran dry: if the server is still speaking, that gap is audible as a crack.
            if (!tts_done && ++underruns % 5 == 1) ESP_LOGW(kTag, "Audio ran out mid-reply (%d times); network jitter", underruns.load());
            playing = false;
            continue;
        }
        esp_audio_dec_in_raw_t raw = {packet.data, static_cast<uint32_t>(packet.size), 0, ESP_AUDIO_DEC_RECOVERY_NONE};
        esp_audio_dec_out_frame_t out = {pcm.data(), static_cast<uint32_t>(pcm.size()), 0, 0};
        esp_audio_dec_info_t info = {};
        esp_audio_err_t decoded = decoder ? esp_opus_dec_decode(decoder, &raw, &out, &info) : ESP_AUDIO_ERR_FAIL;
        if (decoded == ESP_AUDIO_ERR_OK) {
            played_samples += out.decoded_size / 2;
            WriteSpeaker(reinterpret_cast<int16_t*>(pcm.data()), out.decoded_size / 2);
        } else if (++decode_errors % 20 == 1) {
            ESP_LOGE(kTag, "Opus decode failed (%d), %d packets so far", static_cast<int>(decoded), decode_errors.load());
        }
        free(packet.data);
        --pending_audio;
    }
}

void StartVoice() {
    InitCamera();  // optional: without it BitBot simply has no photo tool to offer
    if (!InitAudio()) { ESP_LOGE(kTag, "No audio: voice disabled"); return; }
    ws_events = xEventGroupCreate();
    playback = xQueueCreate(64, sizeof(Packet));
    SetVolume(ReadPrefs().volume);
    // Opus encoding and MultiNet need deep stacks. Audio work stays on core 1, away from Wi-Fi on core 0.
    xTaskCreatePinnedToCore(MicTask, "mic", 40 * 1024, nullptr, 6, nullptr, 1);
    xTaskCreatePinnedToCore(SpeakerTask, "speaker", 24 * 1024, nullptr, 7, nullptr, 1);
    xTaskCreate(VoiceTask, "voice", 6144, nullptr, 5, nullptr);
}
void PressVoiceButton() { button_pressed = true; }
bool VoiceActive() { const Voice v = voice.load(); return v != Voice::Off && v != Voice::Idle; }
}
