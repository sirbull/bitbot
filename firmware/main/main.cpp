#include "bitbot.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" void app_main() {
    using namespace bitbot;
    if (!LoadSettings()) {
        ESP_LOGE("bitbot", "Stored configuration could not be loaded. NVS was preserved; inspect over USB before recovery.");
        return;
    }
    gpio_config_t button = {};
    button.pin_bit_mask = 1ULL << kSetupButton;
    button.mode = GPIO_MODE_INPUT; button.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&button));
    ESP_LOGI("bitbot", "Commissioning 0.1.0; free heap %u; free PSRAM %u", static_cast<unsigned>(esp_get_free_heap_size()), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    InitNetwork();
    if (cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(shared.document, "networks")) == 0) OpenSetup();
    int64_t pressed_at = 0;
    bool handled = false;
    // GPIO0 only reaches this code after a normal boot. Holding at reset enters ROM.
    while (true) {
        if (gpio_get_level(static_cast<gpio_num_t>(kSetupButton)) == 0) {
            if (!pressed_at) pressed_at = NowMs();
            if (!handled && NowMs() - pressed_at >= 3000) { OpenSetup(); handled = true; }
        } else { pressed_at = 0; handled = false; }
        TickNetwork();
        bool restart;
        { std::lock_guard<std::mutex> lock(shared.mutex); restart = shared.restart_at && NowMs() >= shared.restart_at; }
        if (restart) esp_restart();
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
