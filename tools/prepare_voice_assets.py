#!/usr/bin/env python3
"""Validate MiMo prompt WAV files and build mastered ROMFS PCM assets."""

import argparse
import hashlib
import json
import math
import struct
import wave
from collections import deque
from pathlib import Path

TARGET_RATE = 16000
TARGET_WIDTH = 2
TARGET_CHANNELS = 2
NORMALIZATION_VERSION = 3
TARGET_ACTIVE_RMS_DBFS = -14.0
TARGET_ACTIVE_RMS_TOLERANCE_DB = 0.5
PEAK_CEILING = 29204
VAD_FRAME_MS = 20
VAD_FLOOR_DBFS = -50.0
VAD_RELATIVE_DB = -35.0
VAD_PREROLL_MS = 20
VAD_POSTROLL_MS = 40
FADE_MS = 5
COMPRESSOR_THRESHOLD_DBFS = -18.0
COMPRESSOR_RATIO = 3.0
COMPRESSOR_ATTACK_MS = 5.0
COMPRESSOR_RELEASE_MS = 80.0
LIMITER_LOOKAHEAD_MS = 5
LIMITER_RELEASE_MS = 50.0
MAX_LIMITER_REDUCTION_DB = 8.0
MAKEUP_SEARCH_MIN_DB = -60.0
MAKEUP_SEARCH_MAX_DB = 60.0
MAKEUP_SEARCH_ITERATIONS = 48
TARGET_NAME_MAX = 32
RUNTIME_FILES = {
    "carbon_dioxide_concentration_is": "eco2_intro.pcm",
    "volatile_organic_compound_concentration_is": "tvoc_intro.pcm",
}
REQUIRED = {
    "wake_ack": "我在",
    "temperature_low_warning": "温度过低注意保暖",
    "temperature_high_warning": "温度过高注意高温",
    "current_time_is": "当前时间为",
    "second_unit": "秒",
    "today_goal_due": "你今日目标时间已到",
    "current_temperature_is": "当前温度是",
    "below_zero": "零下",
    "zero": "零",
    "one": "一",
    "two": "二",
    "three": "三",
    "four": "四",
    "five": "五",
    "six": "六",
    "seven": "七",
    "eight": "八",
    "nine": "九",
    "ten": "十",
    "hundred": "百",
    "thousand": "千",
    "ten_thousand": "万",
    "point": "点",
    "degree_celsius": "摄氏度",
    "current_humidity_is": "当前湿度是",
    "percentage": "百分比",
    "current_light_is": "当前光照是",
    "lux": "勒克斯",
    "carbon_dioxide_concentration_is": "二氧化碳浓度是",
    "ppm": "PPM",
    "volatile_organic_compound_concentration_is": "挥发性有机物浓度是",
    "ppb": "PPB",
    "schedule_reminder": "事项提醒",
    "schedule_drink": "喝水",
    "schedule_focus": "专注",
    "schedule_ventilate": "通风",
    "hour_unit": "点",
    "minute_unit": "分",
    "whole_hour": "整",
    "half_hour": "半",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dbfs(value: float) -> float:
    if value <= 0:
        return float("-inf")
    return round(20.0 * math.log10(value / 32768.0), 3)


def amplitude_for_dbfs(level_dbfs: float) -> float:
    return 32768.0 * 10.0 ** (level_dbfs / 20.0)


def rms(samples: list[float] | list[int]) -> float:
    if not samples:
        raise ValueError("cannot measure an empty sample sequence")
    return math.sqrt(math.fsum(float(value) * float(value) for value in samples) /
                     len(samples))


def round_ties_away(value: float) -> int:
    """Round to the nearest integer, with exact ties away from zero."""

    if value >= 0:
        return math.floor(value + 0.5)
    return math.ceil(value - 0.5)


def round_ratio(value: int, numerator: int, denominator: int) -> int:
    """Scale an integer ratio and round half away from zero."""

    if denominator <= 0:
        raise ValueError("denominator must be positive")
    magnitude = abs(value) * numerator
    scaled = (magnitude * 2 + denominator) // (denominator * 2)
    return -scaled if value < 0 else scaled


def quantize_int16(samples: list[float]) -> list[int]:
    output = []
    for sample in samples:
        value = round_ties_away(sample)
        output.append(min(32767, max(-32768, value)))
    return output


def pack_mono(samples: list[int]) -> bytes:
    return struct.pack(f"<{len(samples)}h", *samples)


def resample_linear(samples: list[int], source_rate: int,
                    target_rate: int = TARGET_RATE) -> list[int]:
    """Deterministically resample PCM16 with rational linear interpolation."""

    if source_rate <= 0 or target_rate <= 0:
        raise ValueError("sample rates must be positive")
    if not samples:
        return []
    if source_rate == target_rate:
        return list(samples)

    output_count = len(samples) * target_rate // source_rate
    output = []
    for output_index in range(output_count):
        position = output_index * source_rate
        left = position // target_rate
        fraction = position % target_rate
        if left + 1 >= len(samples) or fraction == 0:
            output.append(samples[left])
            continue
        delta = samples[left + 1] - samples[left]
        output.append(
            samples[left] + round_ratio(delta, fraction, target_rate)
        )
    return output


def detect_active_region(samples: list[int]) -> dict:
    frame_samples = TARGET_RATE * VAD_FRAME_MS // 1000
    frame_rms = [
        rms(samples[start:start + frame_samples])
        for start in range(0, len(samples), frame_samples)
    ]
    if not frame_rms:
        raise ValueError("cannot detect activity in empty audio")

    strongest = max(frame_rms)
    threshold = max(
        amplitude_for_dbfs(VAD_FLOOR_DBFS),
        strongest * 10.0 ** (VAD_RELATIVE_DB / 20.0),
    )
    active_frames = [
        index for index, value in enumerate(frame_rms) if value >= threshold
    ]
    if not active_frames:
        raise ValueError("prompt contains no active speech frames")

    preroll = TARGET_RATE * VAD_PREROLL_MS // 1000
    postroll = TARGET_RATE * VAD_POSTROLL_MS // 1000
    first_frame = active_frames[0]
    last_frame = active_frames[-1]
    start = max(0, first_frame * frame_samples - preroll)
    end = min(len(samples), (last_frame + 1) * frame_samples + postroll)
    return {
        "frame_ms": VAD_FRAME_MS,
        "frame_samples": frame_samples,
        "floor_dbfs": VAD_FLOOR_DBFS,
        "relative_threshold_db": VAD_RELATIVE_DB,
        "strongest_frame_rms": round(strongest, 3),
        "strongest_frame_rms_dbfs": dbfs(strongest),
        "threshold_rms": round(threshold, 3),
        "threshold_dbfs": dbfs(threshold),
        "first_active_frame": first_frame,
        "last_active_frame": last_frame,
        "measurement_start_frame": start,
        "measurement_end_frame": end,
        "measurement_frames": end - start,
        "preroll_ms": VAD_PREROLL_MS,
        "postroll_ms": VAD_POSTROLL_MS,
    }


def apply_boundary_fades(samples: list[int]) -> list[float]:
    fade_samples = min(TARGET_RATE * FADE_MS // 1000, len(samples) // 2)
    output = [float(value) for value in samples]
    if fade_samples == 0:
        return output
    for index in range(fade_samples):
        gain = index / fade_samples
        output[index] *= gain
        output[-index - 1] *= gain
    return output


def compressor_gain(level: float) -> float:
    threshold = amplitude_for_dbfs(COMPRESSOR_THRESHOLD_DBFS)
    if level <= threshold or level <= 0:
        return 1.0
    return (threshold / level) ** (1.0 - 1.0 / COMPRESSOR_RATIO)


def compress_samples(samples: list[float]) -> tuple[list[float], dict]:
    attack = math.exp(-1.0 / (TARGET_RATE * COMPRESSOR_ATTACK_MS / 1000.0))
    release = math.exp(-1.0 / (TARGET_RATE * COMPRESSOR_RELEASE_MS / 1000.0))
    gain = 1.0
    minimum_gain = 1.0
    output = []
    for sample in samples:
        desired = compressor_gain(abs(sample))
        coefficient = attack if desired < gain else release
        gain = coefficient * gain + (1.0 - coefficient) * desired
        minimum_gain = min(minimum_gain, gain)
        output.append(sample * gain)
    reduction = -20.0 * math.log10(minimum_gain) if minimum_gain > 0 else float("inf")
    return output, {
        "threshold_dbfs": COMPRESSOR_THRESHOLD_DBFS,
        "ratio": COMPRESSOR_RATIO,
        "attack_ms": COMPRESSOR_ATTACK_MS,
        "release_ms": COMPRESSOR_RELEASE_MS,
        "maximum_gain_reduction_db": round(reduction, 3),
    }


def future_window_peaks(samples: list[float], lookahead: int) -> list[float]:
    """Return max(abs(samples[i:i + lookahead + 1])) in linear time."""

    peaks = [0.0] * len(samples)
    candidates: deque[int] = deque()
    next_index = 0
    for index in range(len(samples)):
        window_end = min(len(samples), index + lookahead + 1)
        while next_index < window_end:
            level = abs(samples[next_index])
            while candidates and abs(samples[candidates[-1]]) <= level:
                candidates.pop()
            candidates.append(next_index)
            next_index += 1
        while candidates and candidates[0] < index:
            candidates.popleft()
        peaks[index] = abs(samples[candidates[0]]) if candidates else 0.0
    return peaks


def limit_samples(samples: list[float]) -> tuple[list[float], dict]:
    lookahead = TARGET_RATE * LIMITER_LOOKAHEAD_MS // 1000
    release = math.exp(-1.0 / (TARGET_RATE * LIMITER_RELEASE_MS / 1000.0))
    peaks = future_window_peaks(samples, lookahead)
    gain = 1.0
    minimum_gain = 1.0
    attenuated_samples = 0
    output = []
    for sample, future_peak in zip(samples, peaks):
        desired = min(1.0, PEAK_CEILING / future_peak) if future_peak > 0 else 1.0
        if desired < gain:
            gain = desired
        else:
            gain = release * gain + (1.0 - release) * desired
        if gain < 1.0 - 1e-12:
            attenuated_samples += 1
        minimum_gain = min(minimum_gain, gain)
        output.append(sample * gain)
    reduction = -20.0 * math.log10(minimum_gain) if minimum_gain > 0 else float("inf")
    return output, {
        "lookahead_ms": LIMITER_LOOKAHEAD_MS,
        "release_ms": LIMITER_RELEASE_MS,
        "ceiling": PEAK_CEILING,
        "ceiling_dbfs": dbfs(PEAK_CEILING),
        "maximum_gain_reduction_db": round(reduction, 3),
        "attenuated_samples": attenuated_samples,
    }


def render_with_makeup(compressed: list[float], gain_db: float) -> tuple[list[float], dict]:
    gain = 10.0 ** (gain_db / 20.0)
    amplified = [sample * gain for sample in compressed]
    return limit_samples(amplified)


def master_mono(samples: list[int], active: dict) -> tuple[list[int], dict]:
    faded = apply_boundary_fades(samples)
    compressed, compressor = compress_samples(faded)
    start = active["measurement_start_frame"]
    end = active["measurement_end_frame"]
    target = amplitude_for_dbfs(TARGET_ACTIVE_RMS_DBFS)

    low = MAKEUP_SEARCH_MIN_DB
    high = MAKEUP_SEARCH_MAX_DB
    low_output, _ = render_with_makeup(compressed, low)
    high_output, _ = render_with_makeup(compressed, high)
    if rms(low_output[start:end]) > target:
        raise ValueError("active RMS target is below the makeup search range")
    if rms(high_output[start:end]) < target:
        raise ValueError("active RMS target is unreachable under the limiter ceiling")

    for _ in range(MAKEUP_SEARCH_ITERATIONS):
        middle = (low + high) / 2.0
        output, _ = render_with_makeup(compressed, middle)
        if rms(output[start:end]) < target:
            low = middle
        else:
            high = middle

    makeup_gain_db = (low + high) / 2.0
    limited, limiter = render_with_makeup(compressed, makeup_gain_db)
    quantized = quantize_int16(limited)
    active_rms = rms(quantized[start:end])
    active_rms_dbfs = dbfs(active_rms)
    peak = max(abs(value) for value in quantized)
    full_scale = sum(value in (-32768, 32767) for value in quantized)

    if abs(active_rms_dbfs - TARGET_ACTIVE_RMS_DBFS) > TARGET_ACTIVE_RMS_TOLERANCE_DB:
        raise ValueError(
            f"active RMS target missed: {active_rms_dbfs} dBFS"
        )
    if peak > PEAK_CEILING:
        raise ValueError(f"limiter exceeded peak ceiling: {peak}")
    if full_scale:
        raise ValueError("mastered prompt contains full-scale samples")
    if limiter["maximum_gain_reduction_db"] > MAX_LIMITER_REDUCTION_DB:
        raise ValueError(
            "limiter is working too heavily: "
            f"{limiter['maximum_gain_reduction_db']} dB"
        )

    return quantized, {
        "fade_ms": FADE_MS,
        "compressor": compressor,
        "makeup_gain_db": round(makeup_gain_db, 6),
        "limiter": limiter,
        "target_active_rms_dbfs": TARGET_ACTIVE_RMS_DBFS,
        "active_rms": round(active_rms, 3),
        "active_rms_dbfs": active_rms_dbfs,
        "post_peak": peak,
        "post_peak_dbfs": dbfs(peak),
        "full_scale_samples": full_scale,
    }


def convert_wav(path: Path, source_name: str | None = None) -> tuple[bytes, dict]:
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        width = source.getsampwidth()
        rate = source.getframerate()
        compression = source.getcomptype()
        frame_count = source.getnframes()
        frames = source.readframes(frame_count)

    if (channels, width, rate, compression) != (1, 2, 24000, "NONE"):
        raise ValueError(
            f"MiMo master must be 24 kHz mono PCM16: {path}"
        )
    if len(frames) != frame_count * width:
        raise ValueError(f"incomplete WAV frames: {path}")

    source_samples = list(struct.unpack(f"<{frame_count}h", frames))
    source_peak = max(abs(value) for value in source_samples) if source_samples else 0
    source_rms = rms(source_samples) if source_samples else 0.0
    if not source_samples or source_peak == 0 or source_rms < 30:
        raise ValueError(f"empty or silent prompt: {path}")
    if source_peak >= 32767:
        raise ValueError(f"clipped prompt: {path}")

    resampled = resample_linear(source_samples, rate)
    pre_peak = max(abs(value) for value in resampled)
    pre_rms = rms(resampled)
    source_active = detect_active_region(resampled)
    trim_start = source_active["measurement_start_frame"]
    trim_end = source_active["measurement_end_frame"]
    trimmed = resampled[trim_start:trim_end]
    active = detect_active_region(trimmed)
    mastered, mastering = master_mono(trimmed, active)
    mono = pack_mono(mastered)

    stereo = bytearray(len(mono) * TARGET_CHANNELS)
    for index, value in enumerate(mastered):
        struct.pack_into("<hh", stereo, index * 4, value, value)

    metadata = {
        "source": source_name or path.name,
        "source_sha256": sha256(path.read_bytes()),
        "source_channels": channels,
        "source_width": width,
        "source_rate": rate,
        "source_frames": frame_count,
        "source_peak": source_peak,
        "source_peak_dbfs": dbfs(source_peak),
        "source_rms": round(source_rms, 3),
        "source_rms_dbfs": dbfs(source_rms),
        "resampler": "rational linear interpolation v1",
        "pre_peak": pre_peak,
        "pre_peak_dbfs": dbfs(pre_peak),
        "pre_rms": round(pre_rms, 3),
        "pre_rms_dbfs": dbfs(pre_rms),
        "source_active_region": source_active,
        "trim": {
            "enabled": True,
            "start_frame": trim_start,
            "end_frame": trim_end,
            "removed_leading_frames": trim_start,
            "removed_trailing_frames": len(resampled) - trim_end,
            "retained_preroll_ms": VAD_PREROLL_MS,
            "retained_postroll_ms": VAD_POSTROLL_MS,
        },
        "active_region": active,
        "mastering": mastering,
        "frames": len(mastered),
    }
    return bytes(stereo), metadata


def generate_cue() -> bytes:
    samples = []
    count = TARGET_RATE * 120 // 1000
    for index in range(count):
        envelope = min(1.0, index / 160, (count - index) / 240)
        value = int(5200 * envelope * math.sin(2 * math.pi * 880 * index / TARGET_RATE))
        samples.append(struct.pack("<hh", value, value))
    return b"".join(samples)


def prepare_assets(
    source_dir: Path,
    output_dir: Path,
    model: str,
    speaker: str,
    source_url: str,
    authorization_note: str,
) -> dict:
    output_dir.mkdir(parents=True, exist_ok=True)
    generation_manifest = source_dir / "mimo-generation.json"
    if not generation_manifest.is_file():
        raise FileNotFoundError(generation_manifest)

    manifest = {
        "format": {
            "sample_rate": TARGET_RATE,
            "sample_width": TARGET_WIDTH,
            "channels": TARGET_CHANNELS,
            "encoding": "signed little-endian headerless PCM",
        },
        "generator": {
            "model": model,
            "speaker": speaker,
            "source_url": source_url,
            "authorization_note": authorization_note,
            "generation_manifest": generation_manifest.name,
            "generation_manifest_sha256": sha256(generation_manifest.read_bytes()),
        },
        "normalization": {
            "version": NORMALIZATION_VERSION,
            "algorithm": "trimmed-boundary active-speech mastering v3",
            "target_active_rms_dbfs": TARGET_ACTIVE_RMS_DBFS,
            "target_tolerance_db": TARGET_ACTIVE_RMS_TOLERANCE_DB,
            "vad": {
                "frame_ms": VAD_FRAME_MS,
                "threshold_rule": "max(-50 dBFS, strongest-frame RMS - 35 dB)",
                "measurement_preroll_ms": VAD_PREROLL_MS,
                "measurement_postroll_ms": VAD_POSTROLL_MS,
                "trimming": "remove excess leading/trailing silence; retain 20 ms pre-roll and 40 ms post-roll",
            },
            "fade_ms": FADE_MS,
            "compressor": {
                "type": "feed-forward sample-peak",
                "threshold_dbfs": COMPRESSOR_THRESHOLD_DBFS,
                "ratio": COMPRESSOR_RATIO,
                "attack_ms": COMPRESSOR_ATTACK_MS,
                "release_ms": COMPRESSOR_RELEASE_MS,
            },
            "limiter": {
                "type": "lookahead peak limiter",
                "lookahead_ms": LIMITER_LOOKAHEAD_MS,
                "release_ms": LIMITER_RELEASE_MS,
                "ceiling": PEAK_CEILING,
                "ceiling_dbfs": dbfs(PEAK_CEILING),
                "maximum_allowed_reduction_db": MAX_LIMITER_REDUCTION_DB,
            },
            "makeup_search_iterations": MAKEUP_SEARCH_ITERATIONS,
            "rounding": "nearest integer, ties away from zero",
            "saturation": "int16 [-32768, 32767]",
        },
        "assets": {},
    }

    cue = generate_cue()
    (output_dir / "cue.pcm").write_bytes(cue)
    manifest["assets"]["cue"] = {
        "text": "880 Hz cue",
        "runtime_file": "cue.pcm",
        "output_sha256": sha256(cue),
        "bytes": len(cue),
    }

    silence = bytes(TARGET_RATE * TARGET_WIDTH * TARGET_CHANNELS * 80 // 1000)
    (output_dir / "silence_80ms.pcm").write_bytes(silence)
    manifest["assets"]["silence_80ms"] = {
        "text": "80 ms silence",
        "runtime_file": "silence_80ms.pcm",
        "output_sha256": sha256(silence),
        "bytes": len(silence),
    }

    runtime_files = [
        RUNTIME_FILES.get(stem, f"{stem}.pcm") for stem in REQUIRED
    ] + ["cue.pcm", "silence_80ms.pcm"]
    if len(runtime_files) != len(set(runtime_files)):
        raise ValueError("duplicate runtime voice filename")
    if len({name.casefold() for name in runtime_files}) != len(runtime_files):
        raise ValueError("case-insensitive runtime voice filename collision")
    for name in runtime_files:
        if Path(name).name != name or not name.isascii() or not name.endswith(".pcm"):
            raise ValueError(f"invalid runtime voice filename: {name}")
        if len(name.encode("utf-8")) > TARGET_NAME_MAX:
            raise ValueError(f"runtime voice filename exceeds NAME_MAX: {name}")

    for stem, text in REQUIRED.items():
        source = source_dir / f"{stem}.wav"
        if not source.is_file():
            raise FileNotFoundError(source)
        pcm, metadata = convert_wav(source, f"{stem}.wav")
        output = output_dir / RUNTIME_FILES.get(stem, f"{stem}.pcm")
        output.write_bytes(pcm)
        metadata.update({
            "text": text,
            "runtime_file": output.name,
            "output_sha256": sha256(pcm),
            "bytes": len(pcm),
        })
        manifest["assets"][stem] = metadata

    manifest_path = output_dir / "assets.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="directory containing named WAV files")
    parser.add_argument("output", type=Path, help="ROMFS output directory")
    parser.add_argument("--model", required=True, help="TTS model/service and version")
    parser.add_argument("--speaker", required=True, help="preset voice identifier")
    parser.add_argument("--source-url", required=True, help="service documentation URL")
    parser.add_argument("--authorization-note", required=True)
    args = parser.parse_args()

    manifest = prepare_assets(
        args.source,
        args.output,
        args.model,
        args.speaker,
        args.source_url,
        args.authorization_note,
    )
    print(f"prepared {len(manifest['assets'])} assets in {args.output}")


if __name__ == "__main__":
    main()
