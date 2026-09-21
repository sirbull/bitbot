#include "bitbot.h"
#include "display.h"
#include "voice.h"
#include "xiaozhi.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace bitbot {
// What the screen tells the user to do next, as rotating {small, big} pages.
static std::vector<Page> Guide(State state, const std::string& ssid, const std::string& password) {
    switch (state) {
        case State::Setup: return {{"1. Join Wi-Fi", ssid}, {"Password", password}, {"2. Follow setup", "on your phone"}};
        case State::Booting:
        case State::Connecting: return {{"Connecting to", "Wi-Fi ..."}};
        case State::Offline: return {{"No Wi-Fi found", "Retrying"}, {"New network? Hold", "BOOT 3 sec"}};
        case State::Error: return {{"Something failed", "See USB log"}};
        case State::Idle: break;
    }
    const auto xiaozhi = GetXiaozhiStatus();
    switch (xiaozhi.pairing) {
        case Pairing::Checking: return {{"Connecting to", "XiaoZhi ..."}};
        case Pairing::Code: return {{"1. Go to", "xiaozhi.me"}, {"2. Tap Console", "and sign in"}, {"3. Add a device", "with the code"}, {"Your code is", xiaozhi.code}};
        case Pairing::Paired: return {{"Say \"Hey Robot\"", ""}};
        case Pairing::Failed: return {{"Can't reach", "XiaoZhi"}, {"Retrying", "shortly"}};
        case Pairing::Off: break;
    }
    return {{"BitBot is", "online"}};
}
}

extern "C" void app_main() {
    using namespace bitbot;
    if (!LoadSettings()) {
        ESP_LOGE("bitbot", "Stored configuration could not be loaded. NVS was preserved; inspect over USB before recovery.");
        return;
    }
    gpio_config_t button = {};
    button.pin_bit_mask = 1ULL << kSetupButton | 1ULL << kExternalSetupButton;
    button.mode = GPIO_MODE_INPUT; button.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&button));
    ESP_LOGI("bitbot", "Commissioning 0.1.0; free heap %u; free PSRAM %u", static_cast<unsigned>(esp_get_free_heap_size()), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    InitNetwork();
    InitDisplay();
    StartXiaozhi();
    StartVoice();
    if (cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(shared.document, "networks")) == 0) OpenSetup();
    else StartStation();
    int64_t pressed_at = 0;
    bool handled = false;
    // GPIO0 only reaches this code after a normal boot. Holding at reset enters ROM.
    while (true) {
        if (gpio_get_level(static_cast<gpio_num_t>(kSetupButton)) == 0 || gpio_get_level(static_cast<gpio_num_t>(kExternalSetupButton)) == 0) {
            if (!pressed_at) pressed_at = NowMs();
            if (!handled && NowMs() - pressed_at >= 3000) { OpenSetup(); handled = true; }
        } else {
            // Any press that did not reach the 3 s setup hold starts or ends a conversation.
            if (pressed_at && !handled && NowMs() - pressed_at >= 30) {
                ESP_LOGI("bitbot", "Button released after %d ms", static_cast<int>(NowMs() - pressed_at));
                PressVoiceButton();
            }
            pressed_at = 0; handled = false;
        }
        TickNetwork();
        State state; std::string ssid, password;
        { std::lock_guard<std::mutex> lock(shared.mutex); state = shared.state; ssid = shared.setup_ssid; password = shared.setup_password; }
        SetDisplayPages(Guide(state, ssid, password));
        // Sleep: while idle, waiting for "Hey Robot", the screen dims. A conversation wakes it.
        constexpr int64_t kDimAfterMs = CONFIG_BITBOT_DISPLAY_DIM_SECONDS * 1000LL;
        static int64_t awake_since = 0;
        if (VoiceActive() || state != State::Idle) awake_since = NowMs();
        SetDisplayDim(kDimAfterMs > 0 && NowMs() - awake_since > kDimAfterMs);
        TickDisplay(state);
        bool restart;
        { std::lock_guard<std::mutex> lock(shared.mutex); restart = shared.restart_at && NowMs() >= shared.restart_at; }
        if (restart) esp_restart();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
