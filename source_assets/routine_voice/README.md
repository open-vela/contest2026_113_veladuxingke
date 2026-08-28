# Routine voice WAV masters

These 40 WAV files are the selected MiMo source masters for fixed Mandarin
wake acknowledgement, temperature warnings, sensor replies, time, and
schedule announcements. The earlier 16-prompt mastering v2 firmware passed
hardware playback validation on 2026-08-11. The expanded prompt set and
trimmed-boundary mastering v3 are host-approved candidates pending a new
firmware hardware validation.

Generation settings are fixed by `tools/generate_mimo_voice_wavs.py`:

- service/model: Xiaomi MiMo `mimo-v2.5-tts`
- preset voice: `冰糖`
- format: 24 kHz, 16-bit, mono WAV
- request style: the fixed Mandarin low-dynamic-range concatenation instruction
  stored in the generator and `mimo-generation.json`
- service documentation:
  <https://mimo.mi.com/docs/zh-CN/quick-start/usage-guide/audio/speech-synthesis-v2.5>

The API key is a user-provided credential used only at generation time. It is
not stored in these files, the manifest, the firmware, or normal logs. The
user has stated that these generated clips are authorized for this contest
entry; that statement does not imply redistribution rights for the MiMo model
or service itself.

`mimo-generation.json` records the fixed request identity, per-file source
SHA-256, WAV metadata, and non-secret response identifiers. Isolated `点`, `七`,
`三`, `千`, and `万` had unsuitable prosody, so their selected masters were
extracted at quiet zero-crossing boundaries from user-approved contextual
utterances. The source hashes, exact frame boundaries, and listening results
are recorded in the manifest. The user also approved representative complete
temperature, humidity, light, eCO2, and TVOC concatenations before selection.

`tools/prepare_voice_assets.py` validates and resamples these sources, removes
only excess leading/trailing silence while retaining 20 ms pre-roll and 40 ms
post-roll, applies deterministic mastering, and emits 16 kHz, 16-bit,
headerless dual-mono PCM for ROMFS. The resulting `assets.json` locks every
source/output hash and processing parameter. Host checks and a successful
firmware build do not replace wake/query accuracy, playback, or deferred
stress validation on the R528 board.
