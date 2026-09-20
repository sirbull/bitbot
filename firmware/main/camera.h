#pragma once
#include <string>
namespace bitbot {
// OV2640/OV3660 on the XIAO Sense expansion board. Nothing is captured unless the assistant asks.
bool InitCamera();   // false when no camera is fitted; everything else keeps working
bool CameraReady();
// Takes one photo, shows it on the screen, and asks XiaoZhi's vision service about it.
// Returns the service's answer, or an empty string if anything failed.
std::string CameraExplain(const std::string& url, const std::string& token, const std::string& question);
}
