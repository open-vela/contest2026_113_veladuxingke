Runtime voice assets are generated from the tracked MiMo WAV masters in
source_assets/routine_voice. Generation uses Xiaomi MiMo mimo-v2.5-tts with
the preset voice 冰糖 and the fixed style recorded in mimo-generation.json.
The service and API key are generation-time inputs and are not included in the
firmware.

Output format: signed little-endian headerless PCM, 16000 Hz, 16-bit,
dual-mono stereo. Mastering v3 measures active speech with 20 ms frames,
removes excess edge silence while retaining 20 ms pre-roll and 40 ms post-roll,
applies 5 ms boundary fades, a deterministic feed-forward compressor, makeup
gain, and a 5 ms lookahead limiter. Every speech fragment targets -14 dBFS
active RMS and remains at or below the 29204 sample ceiling (about -1 dBFS).
Cue and 80 ms silence are unchanged. The ROMFS set contains 40 speech fragments
for wake acknowledgement, temperature warnings, sensor replies, current time,
and schedule reminders.

assets.json records source and output hashes, active-region bounds, resampling,
compressor, limiter, makeup, RMS, peak, frame, and byte metadata. The source
masters were generated under user authorization. The resulting firmware
remains pending targeted hardware validation.
