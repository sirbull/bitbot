#include "bitbot.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace bitbot {
esp_netif_t* ap_netif = nullptr;
esp_netif_t* sta_netif = nullptr;
std::atomic<bool> got_ip{false};
static Network active;
static int64_t attempt_started = 0, retry_at = 0;
static int profile_index = 0, retry_seconds = 5;
static bool attempting = false, was_connected = false;

int64_t NowMs() { return esp_timer_get_time() / 1000; }
const char* StateName(State state) {
    switch (state) {
        case State::Booting: return "booting";
        case State::Setup: return "setup";
        case State::Connecting: return "connecting";
        case State::Idle: return "idle";
        case State::Offline: return "offline";
        case State::Error: return "error";
    }
    return "error";
}
static void Event(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) got_ip.store(true);
    if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP)) got_ip.store(false);
}
void InitNetwork() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, "bitbot"));
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, Event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, Event, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    uint8_t mac[6]; ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    char name[24]; snprintf(name, sizeof(name), "BitBot-%02X%02X", mac[4], mac[5]);
    shared.setup_ssid = name;
    shared.setup_password = Text(shared.document, "setupPassword");
    shared.state = State::Offline;
}
void OpenSetup() {
    bool already_open;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        already_open = shared.setup;
        shared.setup = true; shared.setup_started = NowMs();
        shared.state = State::Setup;
        if (already_open) return;
        shared.token = RandomHex(24);
        shared.job = "idle"; shared.message.clear();
    }
    esp_wifi_disconnect(); got_ip.store(false); attempting = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    wifi_config_t ap = {};
    memcpy(ap.ap.ssid, shared.setup_ssid.data(), shared.setup_ssid.size());
    ap.ap.ssid_len = shared.setup_ssid.size();
    memcpy(ap.ap.password, shared.setup_password.data(), shared.setup_password.size());
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK; ap.ap.max_connection = 2; ap.ap.channel = 1;
    ap.ap.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    StartDns(); StartPortal();
    // Explicit physical commissioning output, never station passwords/API keys.
    printf("\nBITBOT PHYSICAL SETUP\nNetwork: %s\nSetup password: %s\nOpen http://192.168.4.1\n\n", shared.setup_ssid.c_str(), shared.setup_password.c_str());
}
static void CloseSetup() {
    StopPortal(); StopDns();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    std::lock_guard<std::mutex> lock(shared.mutex);
    shared.setup = false; shared.token.clear(); shared.state = State::Offline;
    shared.connect_requested = false; shared.pending = {};
    shared.job = "idle"; attempting = false; retry_at = 0; profile_index = 0;
}
static bool BeginConnection(const Network& network) {
    esp_wifi_scan_stop();
    esp_wifi_disconnect();
    // Let disconnect events drain before accepting a fresh DHCP event.
    vTaskDelay(pdMS_TO_TICKS(150));
    got_ip.store(false);
    wifi_config_t config = {};
    memcpy(config.sta.ssid, network.ssid.data(), network.ssid.size());
    memcpy(config.sta.password, network.password.data(), network.password.size());
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = network.open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.failure_retry_cnt = 2;
    active = network; attempt_started = NowMs(); attempting = true;
    return esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK && esp_wifi_connect() == ESP_OK;
}
void TickNetwork() {
    bool setup, requested, expired;
    Network pending;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        setup = shared.setup;
        expired = setup && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(shared.document, "networks")) > 0 && NowMs() - shared.setup_started >= kSetupMs;
        requested = shared.connect_requested;
        if (requested) { pending = shared.pending; shared.pending = {}; shared.connect_requested = false; }
    }
    if (expired) { CloseSetup(); return; }
    if (requested && !BeginConnection(pending)) attempt_started = NowMs() - 21000;
    bool connected = got_ip.load();
    esp_netif_ip_info_t info = {};
    wifi_ap_record_t record = {};
    connected = connected && esp_netif_get_ip_info(sta_netif, &info) == ESP_OK && info.ip.addr && esp_wifi_sta_get_ap_info(&record) == ESP_OK;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (connected) {
            shared.connected_ssid = reinterpret_cast<const char*>(record.ssid);
            char address[16]; snprintf(address, sizeof(address), IPSTR, IP2STR(&info.ip)); shared.ip = address;
        } else { shared.connected_ssid.clear(); shared.ip.clear(); }
    }
    if (connected && attempting) {
        attempting = false; retry_seconds = 5; profile_index = 0;
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (setup) {
            bool saved = SaveNetwork(active);
            shared.job = saved ? "connected" : "failed";
            shared.message = saved ? "Network saved. Wi-Fi and IP address confirmed; internet access has not been checked." : "Connected, but the network could not be saved. Previous settings are unchanged.";
        } else shared.state = State::Idle;
        active.password.clear();
    } else if (attempting && NowMs() - attempt_started > 20000) {
        esp_wifi_disconnect(); attempting = false; active.password.clear();
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (setup) { shared.job = "failed"; shared.message = "Could not connect. Check the password and try again. Saved networks are unchanged."; }
        else shared.state = State::Offline;
    }
    if (!setup && !connected && !attempting && NowMs() >= retry_at) {
        Network next;
        bool available = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            const auto* networks = cJSON_GetObjectItemCaseSensitive(shared.document, "networks");
            int count = cJSON_GetArraySize(networks);
            if (count && profile_index < count) {
                const auto* item = cJSON_GetArrayItem(networks, profile_index++);
                next = {Text(item, "ssid"), Text(item, "password"), cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "open")) != 0};
                available = true; shared.state = State::Connecting;
            } else {
                profile_index = 0; retry_at = NowMs() + retry_seconds * 1000;
                retry_seconds = std::min(retry_seconds * 2, 600);
            }
        }
        if (available && !BeginConnection(next)) attempt_started = NowMs() - 21000;
    }
    if (connected != was_connected) { ESP_LOGI("bitbot", "%s", connected ? "Wi-Fi connected (internet unchecked)" : "Wi-Fi disconnected"); was_connected = connected; }
}
}
