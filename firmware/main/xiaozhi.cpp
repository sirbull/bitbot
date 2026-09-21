#include "xiaozhi.h"
#include <algorithm>
#include <cstring>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

// XiaoZhi device activation, following 78/xiaozhi-esp32 main/ota.cc (Activation-Version 1):
// POST device info to the OTA endpoint; while unpaired the reply carries a 6-digit code and
// a challenge. Poll <endpoint>/activate: 202 = still waiting, 200 = the user added the code.
// The reply also carries MQTT/WebSocket credentials; they are not used or logged yet.
namespace bitbot {
static constexpr const char* kOtaUrl = "https://api.tenclass.net/xiaozhi/ota/";
static constexpr const char* kTag = "bitbot_xiaozhi";
static constexpr int kFailuresBeforeAlarm = 3;  // a single failed check is normal on a weak link
static std::mutex status_mutex;
static XiaozhiStatus status;
static std::string ws_url, ws_token;  // secret: never logged

static void SetStatus(Pairing pairing, const std::string& code = "") {
    std::lock_guard<std::mutex> lock(status_mutex);
    status = {pairing, code};
}
XiaozhiStatus GetXiaozhiStatus() {
    std::lock_guard<std::mutex> lock(status_mutex);
    return status;
}
bool GetXiaozhiWebsocket(std::string& url, std::string& token) {
    std::lock_guard<std::mutex> lock(status_mutex);
    url = ws_url; token = ws_token;
    return status.pairing == Pairing::Paired && !url.empty();
}
static std::string MacAddress() {
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char text[18]; snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return text;
}
// Random v4 UUID, kept in NVS so XiaoZhi sees the same client across reboots.
static std::string ClientId() {
    nvs_handle_t handle;
    if (nvs_open("xiaozhi", NVS_READWRITE, &handle) != ESP_OK) return "";
    char text[37] = {}; size_t length = sizeof(text);
    if (nvs_get_str(handle, "uuid", text, &length) != ESP_OK) {
        uint8_t b[16]; esp_fill_random(b, sizeof(b));
        b[6] = (b[6] & 0x0f) | 0x40; b[8] = (b[8] & 0x3f) | 0x80;
        snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        if (nvs_set_str(handle, "uuid", text) == ESP_OK) nvs_commit(handle);
    }
    nvs_close(handle);
    return text;
}
// Returns the HTTP status (or -1) and up to 8 KiB of the response body.
static int Post(const char* url, const std::string& body, std::string& response) {
    esp_http_client_config_t config = {};
    config.url = url; config.method = HTTP_METHOD_POST; config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;  // TLS verified against the IDF CA bundle
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;
    static const std::string mac = MacAddress(), client_id = ClientId();
    esp_http_client_set_header(client, "Activation-Version", "1");
    esp_http_client_set_header(client, "Device-Id", mac.c_str());
    esp_http_client_set_header(client, "Client-Id", client_id.c_str());
    esp_http_client_set_header(client, "User-Agent", "bitbot/0.1.0");
    esp_http_client_set_header(client, "Accept-Language", "en-US");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    int code = -1;
    response.clear();
    if (esp_http_client_open(client, body.size()) == ESP_OK &&
        esp_http_client_write(client, body.data(), body.size()) == static_cast<int>(body.size()) &&
        esp_http_client_fetch_headers(client) >= 0) {
        code = esp_http_client_get_status_code(client);
        char buffer[512];
        for (int n; response.size() < 8192 && (n = esp_http_client_read(client, buffer, sizeof(buffer))) > 0;) response.append(buffer, n);
    }
    esp_http_client_cleanup(client);
    return code;
}
static std::string DeviceInfo() {
    const auto* app = esp_app_get_description();
    Json info(cJSON_CreateObject());
    cJSON_AddNumberToObject(info.value, "version", 2);
    cJSON_AddStringToObject(info.value, "language", "en-US");
    cJSON_AddNumberToObject(info.value, "flash_size", 8 * 1024 * 1024);
    cJSON_AddStringToObject(info.value, "mac_address", MacAddress().c_str());
    cJSON_AddStringToObject(info.value, "uuid", ClientId().c_str());
    cJSON_AddStringToObject(info.value, "chip_model_name", "esp32s3");
    auto* application = cJSON_AddObjectToObject(info.value, "application");
    cJSON_AddStringToObject(application, "name", "bitbot");
    cJSON_AddStringToObject(application, "version", app->version);
    cJSON_AddStringToObject(application, "idf_version", app->idf_ver);
    auto* board = cJSON_AddObjectToObject(info.value, "board");
    cJSON_AddStringToObject(board, "type", "bitbot");
    cJSON_AddStringToObject(board, "name", "bitbot");
    return Print(info.value);
}
static bool WantsXiaozhi() {
    std::lock_guard<std::mutex> lock(shared.mutex);
    const auto* settings = cJSON_GetObjectItemCaseSensitive(shared.document, "settings");
    return shared.state == State::Idle && strcmp(Text(settings, "provider"), "xiaozhi") == 0;
}
static void Task(void*) {
    const std::string activate_url = std::string(kOtaUrl) + "activate";
    int backoff_s = 5, failures = 0;
    for (;;) {
        if (!WantsXiaozhi()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        if (GetXiaozhiStatus().pairing == Pairing::Off) SetStatus(Pairing::Checking);
        std::string response;
        int http = Post(kOtaUrl, DeviceInfo(), response);
        Json reply(http == 200 ? cJSON_Parse(response.c_str()) : nullptr);
        if (!reply.value) {
            wifi_ap_record_t ap = {};
            const bool have_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
            ESP_LOGW(kTag, "Check failed (HTTP %d); Wi-Fi %d dBm; retry in %d s", http, have_ap ? ap.rssi : 0, backoff_s);
            // Only complain on screen once it keeps failing; one blip on a weak link is normal.
            if (++failures >= kFailuresBeforeAlarm) SetStatus(Pairing::Failed);
            vTaskDelay(pdMS_TO_TICKS(backoff_s * 1000));
            backoff_s = std::min(backoff_s * 2, 300);
            continue;
        }
        failures = 0;
        backoff_s = 5;
        const auto* activation = cJSON_GetObjectItemCaseSensitive(reply.value, "activation");
        const char* code = Text(activation, "code");
        if (!*code) {
            ESP_LOGI(kTag, "Device is paired with XiaoZhi");
            const auto* websocket = cJSON_GetObjectItemCaseSensitive(reply.value, "websocket");
            {
                std::lock_guard<std::mutex> lock(status_mutex);
                ws_url = Text(websocket, "url"); ws_token = Text(websocket, "token");
            }
            if (ws_url.empty()) ESP_LOGW(kTag, "Paired, but the reply has no WebSocket endpoint; voice is unavailable");
            SetStatus(Pairing::Paired);
            break;  // ponytail: nothing uses the connection yet; the voice pipeline will
        }
        ESP_LOGI(kTag, "Activation code %s; waiting for it to be added at xiaozhi.me", code);
        SetStatus(Pairing::Code, code);
        // Same cadence as upstream: up to 10 polls, then ask again (the code may have changed).
        for (int i = 0; i < 10 && WantsXiaozhi(); ++i) {
            int result = Post(activate_url.c_str(), "{}", response);
            if (result == 200) break;
            vTaskDelay(pdMS_TO_TICKS(result == 202 ? 3000 : 10000));
        }
    }
    vTaskDelete(nullptr);
}
std::string XiaozhiDeviceId() { return MacAddress(); }
std::string XiaozhiClientId() { return ClientId(); }
void StartXiaozhi() { xTaskCreate(Task, "xiaozhi", 8192, nullptr, 3, nullptr); }
}
