#pragma once
namespace bitbot {
// Talking to XiaoZhi: local wake phrases ("hey bitbot", "hei botbot") or a short button press open a
// WebSocket session; mic audio goes up as Opus, the reply comes back as Opus and plays on the speaker.
// Runs only while BitBot is online and paired; everything else (setup, pairing) keeps working without it.
void StartVoice();
// Short press of BOOT or the D2 button: start a conversation, or end the current one.
void PressVoiceButton();
// True while a conversation is open (connecting, listening or speaking).
bool VoiceActive();
}
