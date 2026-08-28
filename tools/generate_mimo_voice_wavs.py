#!/usr/bin/env python3
"""Generate the fixed Mandarin routine prompts with Xiaomi MiMo TTS."""

import argparse
import base64
import binascii
import hashlib
import json
import os
import stat
import struct
import sys
import time
import urllib.error
import urllib.request
import wave
from pathlib import Path
from typing import Callable

PAY_AS_YOU_GO_ENDPOINT = "https://api.xiaomimimo.com/v1/chat/completions"
TOKEN_PLAN_ENDPOINT = "https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
MODEL = "mimo-v2.5-tts"
VOICE = "冰糖"
AUDIO_FORMAT = "wav"
STYLE = (
    "请使用清晰、自然、中性的普通话桌面助手播报风格。近距离收音，语速适中，"
    "音量、距离和情绪保持固定，动态范围较小，适合将多个独立短片段拼接。"
    "只朗读下一条 assistant 消息中的原文，不增删、改写或重复任何字词，"
    "不要加入音乐、音效、问候或解释；首尾仅保留很短的自然静音。"
)
PROMPTS = {
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
RETRYABLE_STATUS = {429, 500, 503}
MAX_RESPONSE_BYTES = 16 * 1024 * 1024
MAX_WAV_BYTES = 12 * 1024 * 1024


class SafeRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Reject redirects so credentials can never cross hosts."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise RuntimeError(f"redirect rejected (HTTP {code})")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def endpoint_for_key(key: str) -> tuple[str, str]:
    if key.startswith("tp-"):
        return TOKEN_PLAN_ENDPOINT, "token-plan"
    return PAY_AS_YOU_GO_ENDPOINT, "pay-as-you-go"


def request_body(text: str) -> dict:
    return {
        "model": MODEL,
        "messages": [
            {"role": "user", "content": STYLE},
            {"role": "assistant", "content": text},
        ],
        "audio": {"format": AUDIO_FORMAT, "voice": VOICE},
        "stream": False,
    }


def read_key_file(path: Path) -> str:
    try:
        info = path.lstat()
    except FileNotFoundError as error:
        raise ValueError(f"API key file does not exist: {path}") from error
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ValueError("API key path must be a regular file, not a symlink")
    if info.st_uid != os.getuid():
        raise ValueError("API key file must be owned by the current user")
    if stat.S_IMODE(info.st_mode) & 0o077:
        raise ValueError("API key file permissions must be 0600 or stricter")
    if info.st_size > 64 * 1024:
        raise ValueError("API key file is unexpectedly large")
    key = path.read_text(encoding="utf-8").strip()
    if not key:
        raise ValueError("API key file is empty")
    if "\n" in key or "\r" in key:
        raise ValueError("API key must contain exactly one line")
    return key


def read_limited(response, maximum: int) -> bytes:
    data = response.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError("API response is too large")
    return data


def open_request(request: urllib.request.Request, timeout: float):
    opener = urllib.request.build_opener(SafeRedirectHandler())
    return opener.open(request, timeout=timeout)


def fetch_audio(
    key: str,
    body: dict,
    timeout: float,
    attempts: int,
    open_fn: Callable = open_request,
    sleep_fn: Callable[[float], None] = time.sleep,
    endpoint: str | None = None,
) -> tuple[bytes, dict]:
    endpoint = endpoint or endpoint_for_key(key)[0]
    payload = canonical_json(body)
    for attempt in range(1, attempts + 1):
        request = urllib.request.Request(
            endpoint,
            data=payload,
            headers={
                "api-key": key,
                "Content-Type": "application/json",
                "Accept": "application/json",
                "User-Agent": "r528-routine-voice-generator/1",
            },
            method="POST",
        )
        try:
            with open_fn(request, timeout) as response:
                status = getattr(response, "status", 200)
                if status != 200:
                    raise urllib.error.HTTPError(
                        endpoint, status, "unexpected status", response.headers, None
                    )
                raw_response = read_limited(response, MAX_RESPONSE_BYTES)
            break
        except urllib.error.HTTPError as error:
            if error.code in RETRYABLE_STATUS and attempt < attempts:
                sleep_fn(float(2 ** (attempt - 1)))
                continue
            raise RuntimeError(f"API returned HTTP {error.code}") from None
        except urllib.error.URLError as error:
            reason = type(error.reason).__name__
            raise RuntimeError(f"API connection failed ({reason})") from None
    else:
        raise RuntimeError("API request failed")

    try:
        result = json.loads(raw_response.decode("utf-8"))
        choice = result["choices"][0]
        message = choice["message"]
        audio = message["audio"]
        encoded = audio["data"]
        if not isinstance(encoded, str) or not encoded:
            raise ValueError
        wav_data = base64.b64decode(encoded, validate=True)
    except (UnicodeDecodeError, json.JSONDecodeError, KeyError, IndexError, TypeError, ValueError, binascii.Error):
        raise ValueError("API response does not contain valid Base64 audio data") from None

    metadata = {
        "response_id": result.get("id"),
        "audio_id": audio.get("id") if isinstance(audio, dict) else None,
        "finish_reason": choice.get("finish_reason"),
        "response_model": result.get("model"),
    }
    return wav_data, metadata


def parse_wav(data: bytes) -> dict:
    if len(data) < 44 or len(data) > MAX_WAV_BYTES:
        raise ValueError("WAV size is outside the expected range")
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("response audio is not a RIFF/WAVE file")

    import io

    try:
        with wave.open(io.BytesIO(data), "rb") as source:
            channels = source.getnchannels()
            width = source.getsampwidth()
            rate = source.getframerate()
            compression = source.getcomptype()
            frame_count = source.getnframes()
            frames = source.readframes(frame_count)
    except (wave.Error, EOFError) as error:
        raise ValueError(f"invalid WAV container ({type(error).__name__})") from None

    if compression != "NONE" or width != 2:
        raise ValueError("MiMo WAV must be uncompressed PCM16")
    if channels != 1:
        raise ValueError(f"MiMo WAV must be mono, got {channels} channels")
    if rate != 24000:
        raise ValueError(f"MiMo WAV must be 24000 Hz, got {rate} Hz")
    if frame_count == 0 or len(frames) != frame_count * channels * width:
        raise ValueError("MiMo WAV contains no complete audio frames")

    samples = struct.unpack(f"<{len(frames) // 2}h", frames)
    peak = max(abs(sample) for sample in samples)
    sum_squares = sum(sample * sample for sample in samples)
    rms = int((sum_squares / len(samples)) ** 0.5)
    full_scale = sum(sample in (-32768, 32767) for sample in samples)
    if peak == 0 or rms < 30:
        raise ValueError("MiMo WAV is silent")
    if full_scale:
        raise ValueError("MiMo WAV contains full-scale clipped samples")

    return {
        "channels": channels,
        "sample_width": width,
        "sample_rate": rate,
        "frames": frame_count,
        "duration_ms": round(frame_count * 1000 / rate, 3),
        "peak": peak,
        "rms": rms,
        "full_scale_samples": full_scale,
    }


def write_atomic(path: Path, data: bytes) -> None:
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        fd = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
            0o600,
        )
        with os.fdopen(fd, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def generate(key_file: Path, output_dir: Path, timeout: float, attempts: int) -> dict:
    key = read_key_file(key_file)
    endpoint, account_type = endpoint_for_key(key)
    output_dir.mkdir(parents=True, exist_ok=True)
    if output_dir.resolve().is_relative_to(Path(__file__).resolve().parents[1]):
        print("提示：输出目录位于仓库内；试听确认前请勿提交这些非确定性母版。")

    manifest = {
        "schema_version": 1,
        "endpoint": endpoint,
        "account_type": account_type,
        "model": MODEL,
        "voice": VOICE,
        "audio_format": AUDIO_FORMAT,
        "style": STYLE,
        "prompts": {},
    }
    try:
        for index, (stem, text) in enumerate(PROMPTS.items(), 1):
            target = output_dir / f"{stem}.wav"
            if target.is_file():
                try:
                    wav_data = target.read_bytes()
                    wav_metadata = parse_wav(wav_data)
                except (OSError, ValueError) as error:
                    raise ValueError(
                        f"已有片段 {target.name} 无效；请删除它后重试 ({error})"
                    ) from None
                body = request_body(text)
                manifest["prompts"][stem] = {
                    "text": text,
                    "request_sha256": sha256(canonical_json(body)),
                    "wav_sha256": sha256(wav_data),
                    "wav_bytes": len(wav_data),
                    "wav": wav_metadata,
                    "response_id": None,
                    "audio_id": None,
                    "finish_reason": "reused-local-wav",
                    "response_model": MODEL,
                }
                print(
                    f"[{index:02d}/{len(PROMPTS)}] 复用已有 {stem}: {text} "
                    f"({wav_metadata['duration_ms']:.0f} ms)",
                    flush=True,
                )
                continue

            print(f"[{index:02d}/{len(PROMPTS)}] 正在生成 {stem}: {text}", flush=True)
            body = request_body(text)
            try:
                wav_data, response_metadata = fetch_audio(
                    key, body, timeout, attempts, endpoint=endpoint
                )
            except RuntimeError as error:
                if str(error) == "API returned HTTP 401":
                    prefix = key.split("-", 1)[0] if "-" in key else "unknown"
                    raise RuntimeError(
                        "API returned HTTP 401：认证失败。"
                        f"检测到 key 类型前缀为 {prefix}-，当前使用 {account_type} 地址；"
                        "请确认 key 完整有效，并且 sk- key 来自 API Keys、"
                        "tp- key 来自 Token Plan。"
                    ) from None
                raise
            wav_metadata = parse_wav(wav_data)
            write_atomic(target, wav_data)
            manifest["prompts"][stem] = {
                "text": text,
                "request_sha256": sha256(canonical_json(body)),
                "wav_sha256": sha256(wav_data),
                "wav_bytes": len(wav_data),
                "wav": wav_metadata,
                **response_metadata,
            }
            print(
                f"         完成：{wav_metadata['duration_ms']:.0f} ms, "
                f"peak={wav_metadata['peak']}, rms={wav_metadata['rms']}"
            )
    finally:
        key = ""

    manifest_path = output_dir / "mimo-generation.json"
    write_atomic(
        manifest_path,
        (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8"),
    )
    print(f"已生成 {len(PROMPTS)} 个 WAV：{output_dir}")
    print(f"无密钥生成清单：{manifest_path}")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate all fixed routine voice WAV masters using MiMo V2.5 TTS."
    )
    parser.add_argument("--key-file", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--attempts", type=int, default=3)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.attempts < 1 or args.attempts > 5:
        parser.error("--attempts must be in 1..5")
    try:
        generate(args.key_file, args.output, args.timeout, args.attempts)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"生成失败：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
