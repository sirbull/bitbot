#pragma once
#include <string>
namespace bitbot {
// OV2640/OV3660 on the XIAO Sense expansion board. Nothing is captured unless the assistant asks,
// and no frame is ever written to flash: capture buffers live in PSRAM only and are freed right
// after use (upload or display), the same as every frame in every other build in this repo.
bool InitCamera();   // false when no camera is fitted; everything else keeps working
bool CameraReady();
// Takes one photo, shows it full-screen, and asks XiaoZhi's vision service about it.
// Returns the service's answer, or an empty string if anything failed.
std::string CameraExplain(const std::string& url, const std::string& token, const std::string& question);
// Shows a live camera feed for a while: the whole frame, full width, about 10 fps. Returns false if the camera isn't ready or a
// live view is already running. CameraExplain() can still be called while one runs (that is how
// "what is this?" works with something held up to the camera), and it extends the live view: the two
// only ever hold the camera for one frame at a time, not for the whole live view.
bool StartLiveView();
// The photo CameraExplain() shows stays on screen while the bot talks about it. Call this when the
// spoken answer is over (or the session ends) to give the screen back to the face.
void EndPhotoPreview();
// Ends a running live view early. Returns false if none is running.
bool StopLiveView();
}
