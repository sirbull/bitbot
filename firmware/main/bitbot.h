#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_netif.h>

namespace bitbot {
constexpr int kSetupButton = 0;  // Seeed onboard BOOT, never an external pin.
constexpr size_t kMaxBody = 12288;
constexpr int64_t kSetupMs = 10 * 60 * 1000;
enum class State { Booting, Setup, Connecting, Idle, Offline, Error };
struct Json {
    cJSON* value;
    explicit Json(cJSON* ptr = nullptr) : value(ptr) {}
    ~Json() { cJSON_Delete(value); }
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
};
struct Network { std::string ssid, password; bool open = false; };
struct Shared {
    std::mutex mutex;
    cJSON* document = nullptr;
    State state = State::Booting;
    std::string setup_ssid, setup_password, token, connected_ssid, ip;
    std::string job = "idle", message;
    Network pending;
    bool connect_requested = false;
    int64_t restart_at = 0;
    bool setup = false;
    int64_t setup_started = 0;
};
extern Shared shared;
extern esp_netif_t* ap_netif;
extern esp_netif_t* sta_netif;
extern std::atomic<bool> got_ip;
int64_t NowMs();
const char* Text(const cJSON* object, const char* key);
std::string Print(const cJSON* object);
std::string RandomHex(size_t bytes);
bool LoadSettings();
bool Commit(cJSON* candidate);  // Caller owns candidate and holds mutex.
bool ValidateSettings(const cJSON* input, std::string& error);
bool ValidateNetwork(const cJSON* input, Network& network, std::string& error);
cJSON* PublicSettings();  // Caller holds mutex; caller owns result.
bool SaveSettings(const cJSON* input, std::string& error);
bool SaveNetwork(const Network& network);
void InitNetwork();
void StartStation();
void OpenSetup();
void TickNetwork();
void StartPortal();
void StopPortal();
void StartDns();
void StopDns();
const char* StateName(State state);
}
