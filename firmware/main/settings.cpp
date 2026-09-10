#include "bitbot.h"
#include "generated/settings_schema.h"
#include <cmath>
#include <cctype>
#include <cstring>
#include <esp_random.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace bitbot {
Shared shared;
const char* Text(const cJSON* object, const char* key) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}
std::string Print(const cJSON* object) {
    char* text = cJSON_PrintUnformatted(object);
    std::string result = text ? text : "";
    cJSON_free(text);
    return result;
}
std::string RandomHex(size_t count) {
    std::string result;
    const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) {
        uint8_t byte; esp_fill_random(&byte, sizeof(byte));
        result += hex[byte >> 4]; result += hex[byte & 15];
    }
    return result;
}
static bool PlainText(const char* value, bool multiline = true) {
    if (!value) return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        if (*p == 127 || (*p < 32 && !(multiline && (*p == 9 || *p == 10 || *p == 13)))) return false;
    }
    return true;
}
static bool ServiceUrl(const std::string& url) {
    size_t start = std::string::npos;
    for (const char* prefix : {"https://", "http://", "wss://", "ws://"}) {
        if (url.rfind(prefix, 0) == 0) start = strlen(prefix);
    }
    if (start == std::string::npos || start == url.size()) return false;
    const auto authority = url.substr(start, url.find('/', start) - start);
    return !authority.empty() && authority.find_first_of("@?# \\\t\n\r") == std::string::npos && url.find_first_of("?# \\\t\n\r") == std::string::npos;
}
static bool LanguageTag(const char* value) {
    if (!strcmp(value, "auto") || !strcmp(value, "auto-all")) return true;
    size_t segment = 0, segments = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value);; ++p) {
        if (*p == '-' || *p == 0) {
            if (segment == 0 || segment > 8 || (segments == 0 && segment < 2)) return false;
            ++segments; segment = 0;
            if (*p == 0) return true;
            continue;
        }
        if ((segments == 0 && !std::isalpha(*p)) || (segments > 0 && !std::isalnum(*p))) return false;
        ++segment;
    }
}
bool ValidateSettings(const cJSON* input, std::string& error) {
    if (!cJSON_IsObject(input)) { error = "Settings must be an object."; return false; }
    Json schema(cJSON_Parse(kSettingsSchema));
    for (const cJSON* item = input->child; item; item = item->next) {
        const auto* rule = cJSON_GetObjectItemCaseSensitive(schema.value, item->string);
        if (!rule) { error = "Unknown setting."; return false; }
        const std::string type = Text(rule, "type");
        bool valid = true;
        if (type == "string" || type == "enum" || type == "url") {
            valid = cJSON_IsString(item) && PlainText(item->valuestring);
            if (valid) {
                const auto* max = cJSON_GetObjectItemCaseSensitive(rule, "maxBytes");
                const auto* min = cJSON_GetObjectItemCaseSensitive(rule, "minBytes");
                const auto length = strlen(item->valuestring);
                valid = (!max || length <= max->valuedouble) && (!min || (length >= min->valuedouble && std::string(item->valuestring).find_first_not_of(" \t\r\n") != std::string::npos));
                if (type == "enum") {
                    valid = false;
                    const auto* values = cJSON_GetObjectItemCaseSensitive(rule, "values");
                    for (const auto* option = values->child; option; option = option->next) if (strcmp(item->valuestring, option->valuestring) == 0) valid = true;
                }
                if (type == "url" && length) valid = valid && ServiceUrl(item->valuestring);
                if (!strcmp(Text(rule, "format"), "language")) valid = valid && LanguageTag(item->valuestring);
            }
        } else if (type == "boolean") valid = cJSON_IsBool(item);
        else {
            const double value = item->valuedouble;
            valid = cJSON_IsNumber(item) && std::isfinite(value) && value >= cJSON_GetObjectItemCaseSensitive(rule, "min")->valuedouble && value <= cJSON_GetObjectItemCaseSensitive(rule, "max")->valuedouble && (type != "integer" || std::floor(value) == value);
        }
        if (!valid) { error = std::string("Invalid setting: ") + item->string; return false; }
    }
    return true;
}
bool ValidateNetwork(const cJSON* input, Network& network, std::string& error) {
    if (!cJSON_IsObject(input)) { error = "Invalid network request."; return false; }
    for (const cJSON* item = input->child; item; item = item->next) {
        if (strcmp(item->string, "ssid") && strcmp(item->string, "password") && strcmp(item->string, "open")) { error = "Unknown network field."; return false; }
    }
    const auto* ssid = cJSON_GetObjectItemCaseSensitive(input, "ssid");
    const auto* password = cJSON_GetObjectItemCaseSensitive(input, "password");
    const auto* open = cJSON_GetObjectItemCaseSensitive(input, "open");
    if (!cJSON_IsString(ssid) || !PlainText(ssid->valuestring, false) || !strlen(ssid->valuestring) || strlen(ssid->valuestring) > 32 || (password && !cJSON_IsString(password)) || (open && !cJSON_IsBool(open))) { error = "Invalid network name or password."; return false; }
    network = {ssid->valuestring, Text(input, "password"), cJSON_IsTrue(open) != 0};
    bool valid = network.open ? network.password.empty() : network.password.size() >= 8 && network.password.size() <= 64;
    for (unsigned char ch : network.password) {
        if (ch < 32 || ch > 126 || (network.password.size() == 64 && !std::isxdigit(ch))) valid = false;
    }
    if (!valid) error = "Use an 8-63 character Wi-Fi password, or select an open network.";
    return valid;
}
bool Commit(cJSON* candidate) {
    const auto serialized = Print(candidate);
    if (serialized.empty() || serialized.size() > kMaxBody) return false;
    cJSON* copy = cJSON_Duplicate(candidate, true);
    if (!copy) return false;
    nvs_handle_t handle;
    if (nvs_open("bitbot", NVS_READWRITE, &handle) != ESP_OK) { cJSON_Delete(copy); return false; }
    esp_err_t result = nvs_set_str(handle, "document", serialized.c_str());
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) { cJSON_Delete(copy); return false; }
    cJSON_Delete(shared.document); shared.document = copy;
    return true;
}
bool LoadSettings() {
    // Never erase stored credentials on an NVS initialization error.
    if (nvs_flash_init() != ESP_OK) return false;
    nvs_handle_t handle;
    if (nvs_open("bitbot", NVS_READWRITE, &handle) != ESP_OK) return false;
    size_t length = 0;
    auto result = nvs_get_str(handle, "document", nullptr, &length);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        Json doc(cJSON_CreateObject());
        cJSON_AddNumberToObject(doc.value, "schemaVersion", 1);
        cJSON_AddItemToObject(doc.value, "settings", cJSON_Parse(kDefaults));
        cJSON_AddObjectToObject(doc.value, "keys");
        cJSON_AddArrayToObject(doc.value, "networks");
        cJSON_AddStringToObject(doc.value, "setupPassword", RandomHex(8).c_str());
        return Commit(doc.value);
    }
    if (result != ESP_OK || length > kMaxBody + 1 || length == 0) { nvs_close(handle); return false; }
    std::string content(length, '\0');
    result = nvs_get_str(handle, "document", content.data(), &length); nvs_close(handle);
    if (result != ESP_OK) return false;
    Json doc(cJSON_Parse(content.c_str()));
    const auto* version = cJSON_GetObjectItemCaseSensitive(doc.value, "schemaVersion");
    auto* settings = cJSON_GetObjectItemCaseSensitive(doc.value, "settings");
    const auto* networks = cJSON_GetObjectItemCaseSensitive(doc.value, "networks");
    const auto* keys = cJSON_GetObjectItemCaseSensitive(doc.value, "keys");
    std::string error;
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 || !ValidateSettings(settings, error) || !cJSON_IsArray(networks) || cJSON_GetArraySize(networks) > 5 || !cJSON_IsObject(keys) || strlen(Text(doc.value, "setupPassword")) != 16) return false;
    Json defaults(cJSON_Parse(kDefaults));
    bool migrated = false;
    for (auto* item = defaults.value->child; item; item = item->next) {
        if (!cJSON_GetObjectItemCaseSensitive(settings, item->string)) {
            cJSON_AddItemToObject(settings, item->string, cJSON_Duplicate(item, true));
            migrated = true;
        }
    }
    for (const auto* network = networks->child; network; network = network->next) { Network parsed; if (!ValidateNetwork(network, parsed, error)) return false; }
    for (const auto* key = keys->child; key; key = key->next) if (!cJSON_IsString(key) || strlen(key->valuestring) > 256) return false;
    if (migrated) return Commit(doc.value);
    shared.document = cJSON_Duplicate(doc.value, true);
    return shared.document != nullptr;
}
cJSON* PublicSettings() {
    auto* output = cJSON_CreateObject();
    const auto* settings = cJSON_GetObjectItemCaseSensitive(shared.document, "settings");
    cJSON_AddItemToObject(output, "settings", cJSON_Duplicate(settings, true));
    const auto* keys = cJSON_GetObjectItemCaseSensitive(shared.document, "keys");
    const std::string provider = Text(settings, "provider");
    const std::string speech_choice = Text(settings, "speechProvider");
    const std::string speech_provider = speech_choice == "same" ? provider : speech_choice;
    cJSON_AddBoolToObject(output, "hasApiKey", strlen(Text(keys, provider.c_str())) > 0);
    cJSON_AddBoolToObject(output, "hasSpeechApiKey", strlen(Text(keys, speech_provider.c_str())) > 0);
    auto* providers = cJSON_AddArrayToObject(output, "keyProviders");
    for (const auto* key = keys->child; key; key = key->next) cJSON_AddItemToArray(providers, cJSON_CreateString(key->string));
    return output;
}
bool SaveSettings(const cJSON* input, std::string& error) {
    if (!cJSON_IsObject(input)) { error = "Invalid settings request."; return false; }
    for (const auto* item = input->child; item; item = item->next) if (strcmp(item->string, "settings") && strcmp(item->string, "apiKey") && strcmp(item->string, "clearApiKey") && strcmp(item->string, "speechApiKey") && strcmp(item->string, "clearSpeechApiKey")) { error = "Unknown request field."; return false; }
    const auto* patch = cJSON_GetObjectItemCaseSensitive(input, "settings");
    if (!ValidateSettings(patch, error)) return false;
    Json next(cJSON_Duplicate(shared.document, true));
    auto* settings = cJSON_GetObjectItemCaseSensitive(next.value, "settings");
    for (const auto* item = patch->child; item; item = item->next) cJSON_ReplaceItemInObjectCaseSensitive(settings, item->string, cJSON_Duplicate(item, true));
    const std::string provider = Text(settings, "provider");
    const std::string speech_choice = Text(settings, "speechProvider");
    const std::string speech_provider = speech_choice == "same" ? provider : speech_choice;
    const auto* pitch = cJSON_GetObjectItemCaseSensitive(settings, "pitch");
    const std::string url = Text(settings, "endpoint");
    const std::string speech_url = Text(settings, "speechEndpoint");
    if (provider != "local" && (url.rfind("http://", 0) == 0 || url.rfind("ws://", 0) == 0)) { error = "Cloud providers require HTTPS/WSS."; return false; }
    if (speech_provider != "local" && (pitch->valuedouble != 0 || speech_url.rfind("http://", 0) == 0 || speech_url.rfind("ws://", 0) == 0)) { error = "Cloud speech providers require HTTPS/WSS and do not support numerical pitch."; return false; }
    const auto* key = cJSON_GetObjectItemCaseSensitive(input, "apiKey");
    const auto* clear = cJSON_GetObjectItemCaseSensitive(input, "clearApiKey");
    const auto* speech_key = cJSON_GetObjectItemCaseSensitive(input, "speechApiKey");
    const auto* clear_speech = cJSON_GetObjectItemCaseSensitive(input, "clearSpeechApiKey");
    if ((key && !cJSON_IsString(key)) || (clear && !cJSON_IsBool(clear)) || strlen(Text(input, "apiKey")) > 256 || (speech_key && !cJSON_IsString(speech_key)) || (clear_speech && !cJSON_IsBool(clear_speech)) || strlen(Text(input, "speechApiKey")) > 256) { error = "Invalid API key."; return false; }
    const std::string secret = Text(input, "apiKey");
    const std::string speech_secret = Text(input, "speechApiKey");
    for (const auto& value : {secret, speech_secret}) for (unsigned char ch : value) if (ch <= 32 || ch == 127) { error = "Invalid API key."; return false; }
    if ((!secret.empty() && cJSON_IsTrue(clear)) || (!speech_secret.empty() && cJSON_IsTrue(clear_speech))) { error = "Choose key replacement or removal."; return false; }
    if (speech_provider == provider && (!speech_secret.empty() || cJSON_IsTrue(clear_speech))) { error = "The selected AI and speech services share one API key."; return false; }
    auto* keys = cJSON_GetObjectItemCaseSensitive(next.value, "keys");
    if (cJSON_IsTrue(clear) || !secret.empty()) cJSON_DeleteItemFromObjectCaseSensitive(keys, provider.c_str());
    if (!secret.empty()) cJSON_AddStringToObject(keys, provider.c_str(), secret.c_str());
    if (cJSON_IsTrue(clear_speech) || !speech_secret.empty()) cJSON_DeleteItemFromObjectCaseSensitive(keys, speech_provider.c_str());
    if (!speech_secret.empty()) cJSON_AddStringToObject(keys, speech_provider.c_str(), speech_secret.c_str());
    if (!Commit(next.value)) { error = "Could not save preferences."; return false; }
    return true;
}
bool SaveNetwork(const Network& network) {
    Json next(cJSON_Duplicate(shared.document, true));
    auto* networks = cJSON_GetObjectItemCaseSensitive(next.value, "networks");
    for (int i = cJSON_GetArraySize(networks) - 1; i >= 0; --i) if (network.ssid == Text(cJSON_GetArrayItem(networks, i), "ssid")) cJSON_DeleteItemFromArray(networks, i);
    if (cJSON_GetArraySize(networks) >= 5) return false;
    auto* entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "ssid", network.ssid.c_str());
    cJSON_AddStringToObject(entry, "password", network.password.c_str());
    cJSON_AddBoolToObject(entry, "open", network.open);
    cJSON_InsertItemInArray(networks, 0, entry);
    return Commit(next.value);
}
}
