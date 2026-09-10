# Provider choices and language validation

Research date: 2026-09-08. This milestone saves provider preferences; it does
not make cloud requests or claim tested Norwegian speech. Pricing, free quotas,
model availability, and account eligibility must be rechecked before activation.

| Route | Norwegian / English | Cost and authentication | Integration and tradeoff |
| --- | --- | --- | --- |
| XiaoZhi hosted service | Norwegian UI exists; that does not establish Norwegian STT/TTS quality. Record a bilingual voice trial. | Upstream advertises free personal Qwen usage with service registration; availability is external. | Native Opus/WebSocket or MQTT+UDP pipeline, vision/tools. Service-side configuration may not map to a generic API key/model field. |
| Gemini | TTS documentation lists Bokmål and Nynorsk; test accents, switching, STT, latency, and vision independently. | Some models have limited free tiers; API key required. Do not promise unlimited free voice. | Native Gemini API/Live differs from OpenAI-compatible APIs. Adapter needed; voice/style support is model-dependent. |
| OpenAI | TTS lists Norwegian, while voices are optimized for English. Measure speech recognition and spoken quality separately. | Usage-based API; key/billing independent of a consumer chat subscription. No free quota assumed. | Separate STT + LLM + TTS preserves canonical captions; streaming available. Use server-supported voices/speed; no arbitrary numerical pitch promise. |
| Self-hosted | NB-Whisper is Norwegian-oriented; also validate English using multilingual Whisper. Select and evaluate a separate Norwegian TTS voice. | No per-request vendor fee on your own hardware, but computer, electricity, and maintenance are needed. Optional gateway authentication. | Ollama supplies a model API, **not an entire STT/TTS voice pipeline**. Vision requires a suitable model. A reachable gateway composes speech and model services. |

Sources: [XiaoZhi README](https://github.com/78/xiaozhi-esp32),
[Gemini pricing](https://ai.google.dev/gemini-api/docs/pricing),
[Gemini speech generation](https://ai.google.dev/gemini-api/docs/speech-generation),
[OpenAI speech generation](https://developers.openai.com/api/docs/guides/text-to-speech),
[OpenAI transcription](https://developers.openai.com/api/docs/guides/speech-to-text),
[NB-Whisper model card](https://huggingface.co/NbAiLab/nb-whisper-small),
[Ollama API](https://docs.ollama.com/api/introduction).

## Proposed first voice path

Reuse XiaoZhi's audio/wake-word/transport layer. The setup portal defaults to
XiaoZhi as the simplest first-run choice and does not ask for generic model,
URL, or API-key fields for that route. Start with push-to-talk and a gateway
that supports separate transcription, conversation, and synthesis.
This allows one canonical response to feed both captions and speech, independent
providers, and meaningful error reporting at each stage. XiaoZhi still needs to
pass the Norwegian acceptance trial before a release can promise bilingual
quality. A self-hosted gateway is the free-from-
per-call-charges option; travelling still requires a reachable gateway.

Cloud vision must be a separate intentional request/tool. Local preview never
needs an image upload. Raw idle audio stays on-device. Later audio transport
uses bounded frames/backpressure, verified TLS, cancellation, and service
timeouts. Neither API keys nor provider responses are put into debug logs.

## Acceptance trial before picking defaults

Use at least ten utterances in each language, including Norwegian dialects,
names, numbers, `æ/ø/å`, and language switches within a session. Include
"Hva er klokka?", "Vis meg hva du ser", and a visual question. Record:

- transcription errors and command routing;
- answer language and spoken naturalness (human judgement);
- time to first caption/audio and end-to-end latency;
- streaming interruptions, failure recovery, and caption agreement;
- actual provider usage/cost, privacy terms, and free-tier rejection behavior.

Do not store voice recordings unless the person testing explicitly agrees.
Pitch is only enabled when the chosen TTS adapter advertises it; changing the
playback sample rate is not a pitch-only control. Model IDs and voice IDs remain
editable to avoid a stale hard-coded catalogue.
