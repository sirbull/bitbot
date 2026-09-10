# BitBot -- Project Specification for Codex

> **Purpose of this document:** This file is the persistent project
> brief for Codex and other development tools working on BitBot. Read it
> before making architectural, firmware, hardware, GPIO, display, audio,
> camera, networking, AI-provider, or power-management changes.

## 1. Project vision

BitBot is a small, portable, battery-powered AI assistant built around
the **Seeed Studio XIAO ESP32-S3 Sense**.

It is not a motorized robot. It has no wheels, servos, or locomotion.
The goal is to create a compact character-like AI device with:

-   voice interaction;
-   a reactive animated face;
-   a small color display;
-   a camera that is activated only when useful;
-   cloud-based AI over Wi-Fi;
-   Norwegian and English conversation;
-   spoken responses and simultaneous on-screen captions;
-   local device functions such as time, battery status, and camera
    preview;
-   aggressive but practical power management for portable battery
    operation.

The device should feel like a small character rather than merely a voice
terminal.

Two open-source projects should be investigated as important references:

-   `idevloop/AI_Pin-Wearable_Voice_Assistant_XIAO_ESP32S3_Sense`
-   `78/xiaozhi-esp32`

The first is especially relevant because it targets XIAO ESP32-S3 Sense
with camera and voice-assistant functionality. XiaoZhi is relevant as
the broader AI-assistant framework. BitBot may combine concepts or code
from both where licenses and architecture permit.

Do not assume that either project should simply be copied unchanged.
First determine which should be the primary technical foundation.

------------------------------------------------------------------------

## 2. Core hardware

### Main controller

**Seeed Studio XIAO ESP32-S3 Sense**

Requirements:

-   ESP32-S3 is the main MCU.
-   The Sense camera expansion board/camera must remain supported.
-   PSRAM should be used where appropriate.
-   Wi-Fi is required.
-   USB should remain usable for development/flashing/debugging.
-   GPIO assignments must be verified against official Seeed
    documentation and the actual camera pin usage.

### Camera

Use the camera supplied with the **XIAO ESP32-S3 Sense**.

The camera is **not intended to run continuously**.

Primary camera use cases:

1.  User asks a visual question such as:
    -   "What do you see?"
    -   "Hva ser du?"
    -   "What is this?"
    -   "Hva er dette?"
    -   "Read this."
    -   "Les dette."
2.  User requests a camera preview:
    -   "Show me what you see."
    -   "Vis meg hva du ser."
3.  While BitBot is awake, lightweight local person/face-position
    detection may optionally be used to make the on-screen eyes look
    toward the user.

The implementation must distinguish between:

-   local camera preview;
-   lightweight local visual tracking;
-   cloud vision inference.

Do not send camera images to a cloud service unless the requested
function actually requires cloud vision.

Camera operation should be designed with privacy, memory use, latency,
and battery consumption in mind.

### Display

**1.54-inch ST7789 TFT LCD**

Known characteristics:

-   240 × 240 pixels
-   SPI
-   7-pin style module
-   module pins:
    -   GND
    -   VCC
    -   SCL / SCK
    -   SDA / MOSI
    -   RES / RST
    -   DC
    -   BLK / BL

Owner-confirmed on 2026-09-10: with the pins at the top, the left-to-right
order is **GND, VCC, SCK, SDA, RES, DC, BLK**. This module has **no CS pin**.
Use ST7789 SPI support with chip-select disabled and exclusive bus ownership.
Do not use an SD card on that SPI bus. See `docs/hardware.md` for the current
signal allocation and unverified supply/backlight requirements.

Important:

-   This is **not** the SSD1306 OLED used by some reference projects.
-   BitBot must use the ST7789 as its main UI.
-   Verify the exact module electrical requirements before finalizing
    wiring.
-   Do not infer GPIO assignments from unrelated ESP32 boards.

### Microphone

**INMP441 digital MEMS microphone**

Interface:

-   I2S
-   pins typically include:
    -   VDD
    -   GND
    -   SCK / BCLK
    -   WS / LRCLK
    -   SD
    -   L/R

The microphone is used for:

-   local wake-word detection;
-   capturing user speech after wake-up;
-   sending speech/audio to the selected speech-recognition system where
    required.

Verify the actual module pinout before wiring.

### Audio amplifier

**MAX98357A I2S Class-D amplifier module**

Used for speech/audio output.

Expected signals include:

-   power;
-   GND;
-   I2S BCLK;
-   I2S LRCLK/LRC;
-   I2S DIN;
-   speaker outputs.

The amplifier may be disabled or power-managed when BitBot is inactive
if the hardware permits it.

### Speaker

Small rectangular speaker, approximately:

-   15 × 11 × 3.5--4 mm.

Verify actual impedance and power rating before finalizing amplifier
configuration.

Important: the MAX98357A speaker output is differential/bridge output.
Do not assume either speaker terminal connects to system ground.

### Battery

Current planned cell:

**10440 Li-ion** - nominal voltage: 3.7 V - advertised capacity:
approximately 350 mAh

The device must be portable and battery powered.

Battery runtime is an important design constraint. Measure real-world
current consumption once the prototype works.

### Charging / power module

A USB-C Li-ion charging/power module has been purchased.

Known visible connections from the purchased module include:

-   USB-C input;
-   battery positive;
-   battery negative;
-   output positive;
-   output negative.

**The exact module model, output voltage, charging current, protection
features, and topology must be verified before final power wiring is
approved.**

Do not guess these specifications.

### Main power switch

A self-locking ON/OFF pushbutton has been purchased.

Approximate listing specification:

-   12 × 8 mm
-   latching/self-locking
-   advertised up to 30 V / 1 A

Only one switch will be used.

Prefer an arrangement that allows the battery to charge while BitBot is
switched off, if supported safely by the charging module.

### Battery measurement

BitBot should measure its own battery voltage.

Initial planned implementation:

-   2 × 100 kΩ, 1% resistors;
-   voltage divider from raw battery voltage to a suitable ESP32-S3 ADC
    input;
-   optional \~100 nF capacitor from ADC measurement node to GND for
    filtering.

Concept:

``` text
BAT+
 |
100k
 |
 +---- ADC
 |
100k
 |
GND
```

Software should compensate for the divider ratio and convert voltage to
an approximate Li-ion state of charge.

Do **not** use a simple linear 3.0--4.2 V percentage mapping if a more
realistic Li-ion lookup/curve can be implemented.

The result remains an estimate; load-dependent voltage sag should be
considered.

------------------------------------------------------------------------

## 3. No motors

BitBot does **not** require:

-   DC motors;
-   motor drivers;
-   wheels;
-   servos;
-   locomotion.

Do not reserve GPIO pins or power budget for motors unless the project
requirements are explicitly changed later.

------------------------------------------------------------------------

## 4. Wake word

BitBot should support a local wake phrase, initially:

**"Hey BitBot"**

Desired behavior:

``` text
low-power/idle
      |
"Hey BitBot"
      |
      v
   awake
      |
   listening
      |
 speech/command
```

Wake-word recognition should preferably happen locally on the ESP32-S3
rather than continuously streaming microphone audio to the cloud.

Goals:

-   lower network usage;
-   improved privacy;
-   lower cloud cost;
-   reduced unnecessary processing;
-   reasonable battery consumption.

Investigate practical ESP32-S3 wake-word solutions compatible with the
selected framework and with Norwegian/English usage.

Do not assume that deep sleep is compatible with microphone wake-word
recognition. Determine the lowest practical power state in which the
selected wake-word engine can function.

If true wake-on-voice from a very low-power state requires additional
hardware, document that clearly.

------------------------------------------------------------------------

## 5. Interaction state machine

BitBot should have explicit behavioral states rather than scattered UI
flags.

Initial conceptual states:

``` text
BOOTING
IDLE
SLEEPING / LOW_POWER
WAKING
LISTENING
PROCESSING
THINKING
SPEAKING
CAMERA_PREVIEW
VISION_PROCESSING
OFFLINE
ERROR
LOW_BATTERY
```

State transitions should drive:

-   face animation;
-   display content;
-   camera state;
-   microphone state;
-   amplifier/audio state;
-   Wi-Fi/network activity where practical;
-   backlight behavior;
-   power-management decisions.

Keep the state machine modular and understandable.

------------------------------------------------------------------------

## 6. Reactive face and character UI

The 240 × 240 ST7789 display should normally show a **BitBot face**.

The face is a major feature of the project.

It must be easy to redesign later without rewriting the AI/audio/camera
subsystems.

Prefer a dedicated abstraction such as:

``` cpp
face.setState(FaceState::Listening);
face.setGaze(x, y);
face.setSpeechLevel(level);
face.setCaption(text);
```

The exact API may differ, but UI/face rendering should be decoupled from
AI logic.

### Desired visual behavior

Possible expressions/states include:

-   sleeping;
-   waking;
-   neutral/idle;
-   listening;
-   thinking;
-   speaking;
-   happy;
-   amused;
-   uncertain;
-   confused;
-   surprised;
-   error;
-   low battery;
-   no Wi-Fi.

The face should not necessarily be implemented as a collection of static
full-screen bitmaps.

Where feasible, draw/animate individual components such as:

-   eyes;
-   pupils;
-   eyelids;
-   mouth;
-   eyebrows or equivalent expressive elements.

This makes redesign and animation easier.

### Idle animation

The face may:

-   blink occasionally;
-   make subtle eye movements;
-   react when woken;
-   transition smoothly between important states.

Avoid excessive animation that wastes CPU/battery without improving the
experience.

------------------------------------------------------------------------

## 7. Gaze / user position

When BitBot is awake and camera use is appropriate, the eyes should
optionally react to where a person appears in the camera image.

Example:

-   person appears left of image center -\> pupils move left;
-   person appears right -\> pupils move right;
-   person is higher/lower -\> gaze can shift vertically.

This should preferably use lightweight **local** detection rather than
repeatedly uploading images to a cloud vision model.

Important conflict to handle:

BitBot's camera should not run continuously while the device is asleep.
Therefore gaze tracking is expected primarily while BitBot is in an
awake/interactive state.

A possible policy is:

1.  BitBot is idle in a low-power state.
2.  "Hey BitBot" wakes it.
3.  During the active session, camera-based lightweight tracking may run
    when useful.
4.  After an inactivity timeout, tracking and camera are disabled.
5.  BitBot returns to low-power mode.

Investigate whether continuous camera operation during the entire awake
session is necessary. Prefer intermittent or low-frame-rate tracking if
it substantially improves battery life.

------------------------------------------------------------------------

## 8. Camera preview

The user should be able to say something equivalent to:

**"Vis meg hva du ser."**\
**"Show me what you see."**

BitBot should then show the camera image on the ST7789.

This is a local preview operation and should not require cloud AI.

Requirements:

-   camera initializes/activates;
-   frames are converted/scaled as required for 240 × 240;
-   image is shown on the ST7789;
-   provide a way to exit preview;
-   automatically exit after a sensible timeout if appropriate;
-   release/disable camera resources afterwards where practical.

Memory bandwidth, PSRAM, display SPI speed, frame format, and frame rate
should be considered.

A smooth 30 FPS video feed is not required. Responsiveness and
reasonable battery use are more important.

------------------------------------------------------------------------

## 9. Vision AI

When the user asks a question that genuinely requires visual
understanding, BitBot may:

1.  activate the camera;
2.  capture an appropriate still image;
3.  optionally resize/compress it;
4.  send it over Wi-Fi to a vision-capable AI service;
5.  receive the answer;
6.  speak and display the answer;
7.  deactivate the camera when no longer needed.

Examples:

-   "Hva ser du?"
-   "What is in front of you?"
-   "Hva slags plante er dette?"
-   "Les teksten på denne boksen."
-   "What color is this object?"

Cloud vision should be invoked by intent/tool selection, not
continuously.

------------------------------------------------------------------------

## 10. Languages

BitBot must support:

-   **Norwegian**
-   **English**

Norwegian is a first-class requirement, not an afterthought.

Desired behavior:

-   user can naturally speak Norwegian;
-   user can naturally speak English;
-   BitBot detects or infers the language;
-   BitBot normally answers in the same language;
-   no manual language switch should be required for ordinary
    conversation.

Evaluate Norwegian quality separately for:

-   speech-to-text (STT/ASR);
-   LLM;
-   text-to-speech (TTS);
-   wake-word recognition if relevant.

A provider that has an excellent English demo but poor Norwegian speech
is not an acceptable default.

------------------------------------------------------------------------

## 11. AI architecture

A large language model should **not run locally on the ESP32-S3**.

The ESP32-S3 is responsible primarily for:

-   device state;
-   wake word;
-   audio capture/playback;
-   camera capture;
-   local camera preview;
-   lightweight local visual tracking where feasible;
-   display/face;
-   captions;
-   battery monitoring;
-   Wi-Fi;
-   communication with remote AI services;
-   local tools/functions.

Remote services may provide:

-   STT;
-   LLM reasoning/conversation;
-   TTS;
-   vision.

### Cost target

Prefer:

1.  free tier where practical;
2.  very inexpensive usage-based services;
3.  services that do not require expensive monthly subscriptions.

However, reliability and Norwegian language quality matter.

Do not hard-code BitBot permanently to a single AI vendor if a provider
abstraction can reasonably be implemented.

Desired conceptual architecture:

``` text
Audio input
    |
    v
STT provider
    |
    v
Conversation / tool layer
    |
    +----> local tools
    |
    +----> LLM provider
    |
    +----> vision provider when required
    |
    v
response text
    |
    +----> captions
    |
    v
TTS provider
    |
    v
speaker
```

Investigate what XiaoZhi already provides before creating redundant
infrastructure.

------------------------------------------------------------------------

## 12. Local tools

Not every question should invoke a remote LLM.

BitBot should expose local device functions/tools where sensible.

Examples:

### Time

Questions such as:

-   "Hva er klokka?"
-   "What time is it?"

should use locally maintained/network-synchronized time rather than
requiring an LLM to invent or retrieve the time.

Use NTP after Wi-Fi connection and maintain time locally.

### Date

Same principle for date/day.

### Battery

Examples:

-   "Hvor mye batteri har du igjen?"
-   "What's your battery level?"

should query the ADC battery subsystem.

### Camera preview

"Vis meg hva du ser" should invoke the local camera-preview function.

### Device status

Potential future local tools:

-   Wi-Fi signal;
-   IP/network status;
-   uptime;
-   firmware version;
-   charging state, if hardware exposes it.

If XiaoZhi's MCP/tool system is suitable, investigate using it for these
functions.

------------------------------------------------------------------------

## 13. Speech output and captions

When BitBot speaks, the **same response should also be readable on the
display**.

The user should not have to wait until speech finishes before seeing the
text.

Desired behavior:

-   response text appears progressively or in useful chunks;
-   TTS playback occurs at the same time;
-   face remains visible as much as possible;
-   captions are readable on a 240 × 240 screen;
-   long text scrolls/pages/wraps intelligently.

The spoken response and displayed caption should originate from the same
canonical response text so they do not disagree.

The UI design may reserve a caption area or temporarily overlay captions
while speaking.

Support Norwegian characters correctly:

**æ ø å Æ Ø Å**

UTF-8/font support must be considered.

------------------------------------------------------------------------

## 14. Speech-reactive animation

The face should react while BitBot speaks.

At minimum, mouth animation can be driven by:

-   outgoing audio amplitude/RMS;
-   TTS playback buffer level.

If the chosen TTS service provides reliable phoneme/viseme timing, a
more advanced implementation may be considered later.

Do not make viseme support a hard dependency for the first working
version.

The goal is that the character visually appears to speak rather than
displaying a static face while audio plays.

------------------------------------------------------------------------

## 15. Wi-Fi

BitBot uses Wi-Fi to reach remote services and retrieve network data.

Requirements:

-   initial Wi-Fi provisioning must be practical;
-   reconnect automatically to known networks;
-   gracefully handle unavailable Wi-Fi;
-   show an understandable offline state;
-   avoid repeatedly hammering connection attempts if offline;
-   protect credentials appropriately;
-   make future reconfiguration possible.

Investigate whether XiaoZhi already provides suitable provisioning and
connection management.

------------------------------------------------------------------------

## 16. Power management

Power consumption is a major design constraint because BitBot is
portable.

Do not optimize prematurely at the expense of getting a working
prototype, but design the architecture so components can be
power-managed.

Investigate:

-   ESP32-S3 light sleep;
-   modem/Wi-Fi power saving;
-   wake-word requirements;
-   display backlight dimming/off;
-   camera deinitialization;
-   camera sensor power behavior;
-   MAX98357A shutdown behavior;
-   microphone/wake-word requirements;
-   CPU frequency/dynamic power management;
-   inactivity timeout.

A likely interaction model:

``` text
POWER ON
   |
   v
BOOT
   |
   v
IDLE / LOW POWER
   |
"Hey BitBot"
   |
   v
ACTIVE SESSION
   |
   +--> listen
   +--> think
   +--> speak
   +--> camera/vision if required
   +--> gaze tracking if enabled
   |
inactivity timeout
   |
   v
IDLE / LOW POWER
```

Do not claim a specific battery runtime until measured on real hardware.

------------------------------------------------------------------------

## 17. Privacy behavior

The design should make camera/microphone behavior understandable.

Principles:

-   wake-word processing should preferably be local;
-   ordinary idle audio should not continuously stream to cloud
    services;
-   camera should not continuously upload;
-   cloud vision should be activated only for relevant requests;
-   camera should normally be inactive while BitBot is sleeping;
-   document when audio/images leave the device.

A future UI indicator for cloud/camera activity may be useful.

------------------------------------------------------------------------

## 18. Software architecture goals

Prefer modular components with clear responsibilities.

Potential modules/classes/subsystems:

``` text
Application / StateMachine
WakeWord
AudioInput
SpeechRecognitionProvider
ConversationManager
LlmProvider
VisionProvider
TtsProvider
AudioOutput
CameraManager
PersonTracker
DisplayManager
FaceRenderer
CaptionRenderer
BatteryMonitor
WifiManager
TimeService
ToolRegistry / MCP
PowerManager
Settings / Provisioning
```

Names are illustrative, not mandatory.

Avoid a monolithic main loop containing all behavior.

Use asynchronous/event-driven behavior where appropriate, while
respecting ESP32 memory constraints.

------------------------------------------------------------------------

## 19. Face redesignability

A future developer/designer should be able to change BitBot's appearance
without understanding the AI networking stack.

Separate:

**behavior/state**

from

**visual representation**.

For example, application logic should communicate:

``` text
expression = LISTENING
gazeX = -0.4
gazeY = 0.1
speechLevel = 0.65
caption = "Ja, selvfølgelig."
```

The renderer decides how that looks.

Consider a configurable face geometry/theme system if practical.

Potential future themes:

-   minimal;
-   cute;
-   retro computer;
-   robotic;
-   custom BitBot identity.

Do not imitate copyrighted character artwork directly.

------------------------------------------------------------------------

## 20. Development approach

Do **not** attempt to implement the entire project in one pass.

Recommended phases:

### Phase 0 -- Research and architecture

Before writing major firmware:

1.  Inspect `78/xiaozhi-esp32`.
2.  Inspect
    `idevloop/AI_Pin-Wearable_Voice_Assistant_XIAO_ESP32S3_Sense`.
3.  Check their current licenses.
4.  Determine which should be the primary base.
5.  Read official Seeed XIAO ESP32-S3 Sense documentation.
6.  Verify camera GPIO usage.
7.  Verify available GPIOs.
8.  Verify ST7789 electrical/interface requirements.
9.  Verify INMP441 requirements.
10. Verify MAX98357A requirements.
11. Identify I2S/SPI/GPIO conflicts.
12. Evaluate RAM/PSRAM/flash requirements.
13. Investigate Norwegian-capable STT/TTS/LLM/vision providers.
14. Investigate wake-word options.
15. Document risks.
16. Produce an architecture proposal.

Do not make major implementation changes until this analysis is
complete.

### Phase 1 -- Basic hardware bring-up

Goal:

-   XIAO boots reliably;
-   serial logging;
-   ST7789 works;
-   test UI/face;
-   INMP441 captures audio;
-   MAX98357A plays test audio;
-   camera captures an image;
-   battery ADC works;
-   Wi-Fi connects.

Test each subsystem independently before integrating AI.

### Phase 2 -- UI and state machine

Implement:

-   states;
-   face rendering;
-   blink/idle;
-   listening animation;
-   thinking animation;
-   speaking animation;
-   captions;
-   battery indicator;
-   Wi-Fi indicator where appropriate.

### Phase 3 -- Voice assistant

Implement:

-   wake word;
-   audio capture;
-   STT;
-   LLM;
-   TTS;
-   synchronized captions;
-   Norwegian and English.

### Phase 4 -- Camera / vision

Implement:

-   camera preview;
-   vision intent/tool;
-   cloud vision requests;
-   camera lifecycle management.

### Phase 5 -- Gaze tracking

Investigate and implement lightweight local person/face-position
detection if feasible.

The feature must not destabilize core voice functionality.

### Phase 6 -- Power optimization

Measure current in each state and optimize based on evidence.

------------------------------------------------------------------------

## 21. Hardware verification rules for Codex

These rules are mandatory.

### Never invent GPIO assignments

Before assigning a GPIO:

1.  verify the official XIAO ESP32-S3 Sense pin map;
2.  verify camera/expansion-board pin usage;
3.  check boot/strapping restrictions;
4.  check peripheral conflicts;
5.  check whether the reference firmware already reserves the pin.

Record the final allocation in a hardware document/table.

### Never guess power-module behavior

The exact USB-C battery/boost module must be identified or electrically
verified before final power wiring is approved.

### Do not copy pin numbers from generic ESP32 examples

The XIAO ESP32-S3 Sense has its own pin mapping and camera
configuration.

### Datasheets first

Prefer:

1.  manufacturer datasheets;
2.  official Seeed documentation;
3.  official component documentation;
4.  source code from the selected upstream framework.

Treat random wiring diagrams/blog posts as secondary evidence.

------------------------------------------------------------------------

## 22. Breadboard / prototype documentation

Once GPIO allocation is verified, maintain a definitive wiring table.

Example structure:

  BitBot function   Module pin         XIAO pin   GPIO   Voltage       Notes
  ----------------- ------------------ ---------- ------ ------------- --------
  TFT clock         SCL/SCK            TBD        TBD    3.3 V logic   Verify
  TFT MOSI          SDA/MOSI           TBD        TBD    3.3 V logic   Verify
  TFT DC            DC                 TBD        TBD    3.3 V logic   
  Microphone BCLK   SCK                TBD        TBD    3.3 V         
  Microphone data   SD                 TBD        TBD    3.3 V         
  Amplifier data    DIN                TBD        TBD    3.3 V logic   
  Battery sense     divider midpoint   TBD        TBD    ADC-safe      

`TBD` must remain `TBD` until verified. Do not replace uncertainty with
guesses.

The wiring table should be treated as authoritative over generated
illustrations.

------------------------------------------------------------------------

## 23. AI/provider evaluation

Before choosing cloud providers, compare candidates for:

-   Norwegian STT quality;
-   English STT quality;
-   Norwegian TTS naturalness;
-   English TTS naturalness;
-   latency;
-   streaming support;
-   vision capability;
-   ESP32-friendly protocol/API;
-   free tier;
-   expected low-volume cost;
-   authentication complexity;
-   vendor lock-in;
-   privacy;
-   whether self-hosting is possible.

Prefer an architecture where providers can be replaced.

The cheapest service is not automatically the best choice if Norwegian
speech quality is poor.

------------------------------------------------------------------------

## 24. Error handling

BitBot should fail gracefully.

Examples:

### No Wi-Fi

Display an offline expression/status rather than hanging.

Local functions such as battery and potentially time should remain
available.

### AI service unavailable

Tell the user succinctly rather than silently failing.

### Camera failure

Return to the normal UI and report the error.

### Low battery

Show a low-battery state and avoid starting expensive operations if
voltage becomes unsafe.

### Audio failure

Log useful diagnostics over serial.

------------------------------------------------------------------------

## 25. Logging and debugging

During development, provide useful serial logging for:

-   state transitions;
-   Wi-Fi;
-   audio initialization;
-   camera initialization;
-   display initialization;
-   battery voltage;
-   wake-word events;
-   API request status;
-   memory/PSRAM availability;
-   recoverable errors.

Never log:

-   API secrets;
-   Wi-Fi passwords;
-   sensitive authentication tokens.

------------------------------------------------------------------------

## 26. Secrets

Do not commit real API keys or Wi-Fi credentials to Git.

Use the framework's appropriate secrets/configuration mechanism.

Provide example/template configuration files where useful.

------------------------------------------------------------------------

## 27. Definition of a successful first complete prototype

The first major BitBot milestone is achieved when the physical device
can:

1.  boot from battery;
2.  connect to Wi-Fi;
3.  show its animated face;
4.  display battery status;
5.  wait for "Hey BitBot";
6.  visibly react to the wake word;
7.  listen to a spoken Norwegian or English question;
8.  transcribe it correctly enough for conversation;
9.  obtain an AI response over Wi-Fi;
10. display the response as readable captions;
11. speak the same response through the speaker;
12. animate its face while speaking;
13. answer in the same language as the user;
14. show the camera view when asked;
15. use cloud vision when a question requires visual understanding;
16. stop/deactivate camera functionality when it is no longer needed;
17. return to a lower-power idle state after inactivity.

Gaze tracking is desirable but should not block the first complete
voice-assistant milestone.

------------------------------------------------------------------------

## 28. Instructions to Codex

When beginning work on this repository:

1.  Read this entire document.
2.  Inspect the existing repository before changing anything.
3.  Investigate the two upstream projects.
4.  Verify hardware facts against primary documentation.
5.  Do not invent missing specifications.
6.  Clearly distinguish verified facts from assumptions.
7.  Prefer extending proven upstream functionality over rewriting
    working systems.
8.  Keep BitBot-specific hardware/UI code isolated where practical.
9.  Preserve the ability to update/merge relevant upstream changes.
10. Optimize for maintainability, battery operation, and ESP32-S3
    resource limits.
11. Do not implement all phases at once.
12. Compile/test after meaningful changes whenever the toolchain is
    available.
13. Document unresolved hardware questions instead of guessing.
14. Keep Norwegian and English support as core requirements.
15. Keep camera activation intentional and power-aware.
16. Keep the face renderer replaceable/redesignable.
17. Keep captions synchronized with the canonical spoken response.
18. Keep cloud AI providers replaceable where reasonably possible.

### First task for Codex

Before implementing the complete firmware:

> Analyze the requirements in this document and inspect both upstream
> projects. Determine the best technical foundation for BitBot. Verify
> the XIAO ESP32-S3 Sense hardware and camera constraints using official
> documentation. Propose a GPIO/peripheral allocation, identify
> conflicts and technical risks, evaluate the software architecture and
> AI-provider options, and create an implementation plan. Do not
> implement the complete project yet. Present the findings before
> proceeding with major implementation.

------------------------------------------------------------------------

## 29. Reference repositories

-   AI Pin for XIAO ESP32-S3 Sense:\
    `https://github.com/idevloop/AI_Pin-Wearable_Voice_Assistant_XIAO_ESP32S3_Sense`

-   XiaoZhi ESP32:\
    `https://github.com/78/xiaozhi-esp32`

When using code from upstream projects, verify and respect the license
of the exact source being used.

------------------------------------------------------------------------

## 30. Current project name

**BitBot**

Wake phrase:

**"Hey BitBot"**

These may be changed later, so avoid unnecessarily hard-coding branding
throughout unrelated subsystems.
