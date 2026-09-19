#pragma once
#include "bitbot.h"
#include <string>
namespace bitbot {
// Pairing with XiaoZhi runs in its own task once BitBot is online with provider "xiaozhi".
enum class Pairing { Off, Checking, Code, Paired, Failed };
struct XiaozhiStatus { Pairing pairing = Pairing::Off; std::string code; };
void StartXiaozhi();
XiaozhiStatus GetXiaozhiStatus();
// WebSocket endpoint and token from the last successful check; false until paired.
bool GetXiaozhiWebsocket(std::string& url, std::string& token);
std::string XiaozhiDeviceId();
std::string XiaozhiClientId();
}
