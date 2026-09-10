#include "bitbot.h"
#include "display.h"
#include "generated/portal_assets.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <esp_wifi.h>
#include <lwip/sockets.h>

namespace bitbot {
static httpd_handle_t server = nullptr;
static esp_err_t Send(httpd_req_t* req, const cJSON* value, const char* status = "200 OK") {
    const auto output = Print(value);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, output.c_str(), output.size());
}
static esp_err_t Error(httpd_req_t* req, const char* text, const char* status = "400 Bad Request") {
    Json result(cJSON_CreateObject()); cJSON_AddStringToObject(result.value, "error", text);
    return Send(req, result.value, status);
}
static std::string Header(httpd_req_t* req, const char* name) {
    size_t length = httpd_req_get_hdr_value_len(req, name);
    if (length > 256) return "invalid";
    std::string output(length + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, output.data(), output.size()) != ESP_OK) return "";
    output.resize(length); return output;
}
static bool LocalAccess(httpd_req_t* req) {
    sockaddr_in local = {}, peer = {}; socklen_t length = sizeof(local);
    int fd = httpd_req_to_sockfd(req);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &length) != 0 || getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &length) != 0) return false;
    return local.sin_addr.s_addr == inet_addr("192.168.4.1") && (ntohl(peer.sin_addr.s_addr) & 0xffffff00U) == 0xc0a80400U;
}
static bool DuplicateFields(const cJSON* value) {
    if (!value) return false;
    for (auto* a = value->child; a; a = a->next) {
        if (DuplicateFields(a)) return true;
        if (cJSON_IsObject(value)) for (auto* b = a->next; b; b = b->next) if (strcmp(a->string, b->string) == 0) return true;
    }
    return false;
}
static cJSON* Networks() {
    auto* array = cJSON_CreateArray();
    const auto* networks = cJSON_GetObjectItemCaseSensitive(shared.document, "networks");
    for (const auto* item = networks->child; item; item = item->next) {
        auto* network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", Text(item, "ssid"));
        cJSON_AddBoolToObject(network, "open", cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "open")));
        cJSON_AddItemToArray(array, network);
    }
    return array;
}
static esp_err_t Get(httpd_req_t* req, const std::string& uri) {
    std::lock_guard<std::mutex> lock(shared.mutex);
    if (uri == "/api/session") {
        Json value(cJSON_CreateObject()); cJSON_AddStringToObject(value.value, "token", shared.token.c_str()); return Send(req, value.value);
    }
    if (uri == "/api/settings") { Json value(PublicSettings()); return Send(req, value.value); }
    if (uri == "/api/networks") { Json value(Networks()); return Send(req, value.value); }
    if (uri == "/api/status") {
        Json value(cJSON_CreateObject());
        cJSON_AddBoolToObject(value.value, "simulator", false);
        cJSON_AddStringToObject(value.value, "firmware", "0.1.0");
        cJSON_AddStringToObject(value.value, "state", StateName(shared.state));
        cJSON_AddStringToObject(value.value, "connectedSsid", shared.connected_ssid.c_str());
        cJSON_AddStringToObject(value.value, "ip", shared.ip.c_str());
        cJSON_AddStringToObject(value.value, "internet", "unchecked");
        cJSON_AddStringToObject(value.value, "setupSsid", shared.setup_ssid.c_str());
        cJSON_AddNumberToObject(value.value, "uptimeSeconds", NowMs() / 1000);
        auto* job = cJSON_AddObjectToObject(value.value, "job");
        cJSON_AddStringToObject(job, "state", shared.job.c_str()); cJSON_AddStringToObject(job, "message", shared.message.c_str());
        auto* caps = cJSON_AddObjectToObject(value.value, "capabilities");
        for (const char* key : {"assistant", "audio", "camera"}) cJSON_AddBoolToObject(caps, key, false);
        cJSON_AddBoolToObject(caps, "display", DisplayReady());
        return Send(req, value.value);
    }
    if (uri == "/api/scan") {
        if (shared.job == "testing") return Error(req, "Wait for the connection test to finish.", "409 Conflict");
        if (esp_wifi_scan_start(nullptr, true) != ESP_OK) return Error(req, "Wi-Fi scan is busy. Try again.", "503 Service Unavailable");
        uint16_t count = 24;
        std::vector<wifi_ap_record_t> records(count);
        if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) return Error(req, "Scan failed.", "503 Service Unavailable");
        records.resize(count);
        std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.rssi > b.rssi; });
        Json value(cJSON_CreateArray()); std::vector<std::string> seen;
        for (const auto& record : records) {
            std::string ssid(reinterpret_cast<const char*>(record.ssid));
            if (ssid.empty() || std::find(seen.begin(), seen.end(), ssid) != seen.end()) continue;
            // Only advertise personal/open networks supported by this milestone.
            if (record.authmode != WIFI_AUTH_OPEN && record.authmode != WIFI_AUTH_WPA2_PSK && record.authmode != WIFI_AUTH_WPA_WPA2_PSK && record.authmode != WIFI_AUTH_WPA3_PSK && record.authmode != WIFI_AUTH_WPA2_WPA3_PSK) continue;
            seen.push_back(ssid);
            auto* network = cJSON_CreateObject(); cJSON_AddStringToObject(network, "ssid", ssid.c_str());
            cJSON_AddNumberToObject(network, "rssi", record.rssi); cJSON_AddBoolToObject(network, "open", record.authmode == WIFI_AUTH_OPEN); cJSON_AddItemToArray(value.value, network);
        }
        return Send(req, value.value);
    }
    const uint8_t* bytes = nullptr; size_t length = 0;
    if (uri == "/") { bytes = assets::index; length = sizeof(assets::index); httpd_resp_set_type(req, "text/html; charset=utf-8"); }
    if (uri == "/style.css") { bytes = assets::style; length = sizeof(assets::style); httpd_resp_set_type(req, "text/css; charset=utf-8"); }
    if (uri == "/app.js") { bytes = assets::app; length = sizeof(assets::app); httpd_resp_set_type(req, "text/javascript; charset=utf-8"); }
    if (bytes) { httpd_resp_set_hdr(req, "Content-Encoding", "gzip"); return httpd_resp_send(req, reinterpret_cast<const char*>(bytes), length); }
    if (uri.rfind("/api/", 0) == 0) return Error(req, "Not found.", "404 Not Found");
    httpd_resp_set_status(req, "302 Found"); httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/"); return httpd_resp_send(req, "", 0);
}
static esp_err_t Post(httpd_req_t* req, const std::string& uri) {
    const auto token = Header(req, "X-BitBot-Token");
    { std::lock_guard<std::mutex> lock(shared.mutex); if (shared.token.empty() || token != shared.token) return Error(req, "Setup session expired. Reload this page.", "403 Forbidden"); }
    if (Header(req, "Content-Type").rfind("application/json", 0) != 0) return Error(req, "JSON is required.", "415 Unsupported Media Type");
    if (!req->content_len || req->content_len > kMaxBody) return Error(req, "Request is too large or empty.", "413 Content Too Large");
    std::string body(req->content_len, '\0'); size_t received = 0;
    while (received < body.size()) {
        int count = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (count <= 0) return Error(req, "Request timed out.", "408 Request Timeout");
        received += count;
    }
    if (body.find('\0') != std::string::npos || body.find("\\u0000") != std::string::npos) return Error(req, "Invalid JSON string.");
    Json input(cJSON_ParseWithOpts(body.c_str(), nullptr, true));
    if (!cJSON_IsObject(input.value) || DuplicateFields(input.value)) return Error(req, "Invalid JSON object.");
    std::lock_guard<std::mutex> lock(shared.mutex);
    if (shared.restart_at) return Error(req, "Device is restarting.", "409 Conflict");
    if (uri == "/api/settings") {
        std::string error;
        if (!SaveSettings(input.value, error)) return Error(req, error.c_str());
        Json result(PublicSettings()); return Send(req, result.value);
    }
    if (uri == "/api/connect") {
        if (shared.job == "testing") return Error(req, "A connection test is already running.", "409 Conflict");
        Network network; std::string error;
        if (!ValidateNetwork(input.value, network, error)) return Error(req, error.c_str());
        const auto* networks = cJSON_GetObjectItemCaseSensitive(shared.document, "networks");
        bool exists = false;
        for (const auto* item = networks->child; item; item = item->next) exists |= network.ssid == Text(item, "ssid");
        if (!exists && cJSON_GetArraySize(networks) >= 5) return Error(req, "Five networks are saved. Forget one before adding another.");
        shared.pending = network; shared.connect_requested = true; shared.job = "testing"; shared.message = "Testing Wi-Fi connection...";
        Json result(cJSON_CreateObject()); cJSON_AddStringToObject(result.value, "state", "testing"); cJSON_AddStringToObject(result.value, "message", shared.message.c_str()); return Send(req, result.value, "202 Accepted");
    }
    if (uri == "/api/forget") {
        if (shared.job == "testing") return Error(req, "Wait for the connection test to finish.", "409 Conflict");
        const auto* ssid = cJSON_GetObjectItemCaseSensitive(input.value, "ssid");
        if (!cJSON_IsString(ssid)) return Error(req, "Invalid network name.");
        Json next(cJSON_Duplicate(shared.document, true));
        auto* networks = cJSON_GetObjectItemCaseSensitive(next.value, "networks");
        int index = -1;
        for (int i = 0; i < cJSON_GetArraySize(networks); ++i) if (strcmp(ssid->valuestring, Text(cJSON_GetArrayItem(networks, i), "ssid")) == 0) index = i;
        if (index < 0) return Error(req, "Saved network not found.");
        cJSON_DeleteItemFromArray(networks, index);
        if (!Commit(next.value)) return Error(req, "Could not forget network.", "500 Internal Server Error");
        if (shared.connected_ssid == ssid->valuestring) esp_wifi_disconnect();
        Json result(Networks()); return Send(req, result.value);
    }
    if (uri == "/api/finish") {
        if (shared.job == "testing") return Error(req, "Wait for the connection test to finish.", "409 Conflict");
        if (cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(shared.document, "networks")) == 0) return Error(req, "Connect to a network first.");
        shared.restart_at = NowMs() + 1200;
        Json result(cJSON_CreateObject()); cJSON_AddStringToObject(result.value, "message", "Settings saved. BitBot is restarting."); return Send(req, result.value);
    }
    return Error(req, "Not found.", "404 Not Found");
}
static esp_err_t Handle(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; style-src 'self'; script-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    if (!LocalAccess(req)) return Error(req, "Join the BitBot setup hotspot first.", "403 Forbidden");
    const auto host = Header(req, "Host");
    if (host != "192.168.4.1" && host != "192.168.4.1:80") {
        if (req->method != HTTP_GET) return Error(req, "Invalid host.", "403 Forbidden");
        httpd_resp_set_status(req, "302 Found"); httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/"); return httpd_resp_send(req, "", 0);
    }
    const auto origin = Header(req, "Origin");
    if (!origin.empty() && origin != "http://192.168.4.1" && origin != "http://192.168.4.1:80") return Error(req, "Invalid origin.", "403 Forbidden");
    const std::string uri = std::string(req->uri).substr(0, std::string(req->uri).find('?'));
    return req->method == HTTP_GET ? Get(req, uri) : Post(req, uri);
}
void StartPortal() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 10240; config.max_open_sockets = 4; config.lru_purge_enable = true;
    config.recv_wait_timeout = 5; config.send_wait_timeout = 5; config.max_resp_headers = 8;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    for (httpd_method_t method : {HTTP_GET, HTTP_POST}) {
        httpd_uri_t route = {}; route.uri = "/*"; route.method = method; route.handler = Handle;
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    }
}
void StopPortal() { if (server) { httpd_stop(server); server = nullptr; } }
}
