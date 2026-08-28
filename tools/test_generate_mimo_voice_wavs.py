#!/usr/bin/env python3
"""Offline tests for the MiMo routine voice generator."""

import base64
import contextlib
import importlib.util
import io
import json
import os
import stat
import struct
import tempfile
import unittest
import urllib.error
import wave
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("generate_mimo_voice_wavs.py")
SPEC = importlib.util.spec_from_file_location("generate_mimo_voice_wavs", MODULE_PATH)
mimo = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mimo)
TEST_KEY = "mimo-test-secret-never-print"


def make_wav(rate: int = 24000, channels: int = 1, width: int = 2) -> bytes:
    output = io.BytesIO()
    with wave.open(output, "wb") as target:
        target.setnchannels(channels)
        target.setsampwidth(width)
        target.setframerate(rate)
        samples = [1200, -1200] * 1200
        raw = struct.pack(f"<{len(samples)}h", *samples)
        target.writeframes(raw if width == 2 else bytes(len(samples)))
    return output.getvalue()


class FakeResponse:
    def __init__(self, body: bytes, status: int = 200):
        self.body = body
        self.status = status
        self.headers = {}

    def read(self, size: int = -1) -> bytes:
        return self.body if size < 0 else self.body[:size]

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False


class KeyFileTests(unittest.TestCase):
    def test_missing_empty_and_permissive_files_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaises(ValueError):
                mimo.read_key_file(root / "missing")
            empty = root / "empty"
            empty.write_text("", encoding="utf-8")
            empty.chmod(0o600)
            with self.assertRaises(ValueError):
                mimo.read_key_file(empty)
            permissive = root / "permissive"
            permissive.write_text(TEST_KEY, encoding="utf-8")
            permissive.chmod(0o644)
            with self.assertRaises(ValueError):
                mimo.read_key_file(permissive)

    def test_secure_regular_file_is_read(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            key_file = Path(temp) / "key"
            key_file.write_text(TEST_KEY + "\n", encoding="utf-8")
            key_file.chmod(0o600)
            self.assertEqual(mimo.read_key_file(key_file), TEST_KEY)

    def test_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target = root / "target"
            target.write_text(TEST_KEY, encoding="utf-8")
            target.chmod(0o600)
            link = root / "link"
            link.symlink_to(target)
            with self.assertRaises(ValueError):
                mimo.read_key_file(link)


class RequestTests(unittest.TestCase):
    def test_endpoint_matches_key_type(self) -> None:
        self.assertEqual(
            mimo.endpoint_for_key("sk-example"),
            (mimo.PAY_AS_YOU_GO_ENDPOINT, "pay-as-you-go"),
        )
        self.assertEqual(
            mimo.endpoint_for_key("tp-example"),
            (mimo.TOKEN_PLAN_ENDPOINT, "token-plan"),
        )

    def test_request_schema_and_all_exact_prompts(self) -> None:
        for stem, text in mimo.PROMPTS.items():
            body = mimo.request_body(text)
            self.assertEqual(body["model"], "mimo-v2.5-tts")
            self.assertEqual(body["audio"], {"format": "wav", "voice": "冰糖"})
            self.assertIs(body["stream"], False)
            self.assertEqual([item["role"] for item in body["messages"]], ["user", "assistant"])
            self.assertEqual(body["messages"][1]["content"], text, stem)
        self.assertEqual(len(mimo.PROMPTS), 40)
        self.assertEqual(mimo.PROMPTS["wake_ack"], "我在")
        self.assertEqual(
            mimo.PROMPTS["temperature_low_warning"],
            "温度过低注意保暖",
        )
        self.assertEqual(
            mimo.PROMPTS["temperature_high_warning"],
            "温度过高注意高温",
        )
        self.assertEqual(mimo.PROMPTS["current_time_is"], "当前时间为")
        self.assertEqual(mimo.PROMPTS["second_unit"], "秒")
        self.assertEqual(
            mimo.PROMPTS["today_goal_due"], "你今日目标时间已到"
        )
        self.assertEqual(mimo.PROMPTS["current_humidity_is"], "当前湿度是")
        self.assertEqual(mimo.PROMPTS["current_light_is"], "当前光照是")
        self.assertEqual(
            mimo.PROMPTS["carbon_dioxide_concentration_is"],
            "二氧化碳浓度是",
        )
        self.assertEqual(
            mimo.PROMPTS["volatile_organic_compound_concentration_is"],
            "挥发性有机物浓度是",
        )
        self.assertEqual(mimo.PROMPTS["schedule_reminder"], "事项提醒")
        self.assertEqual(mimo.PROMPTS["whole_hour"], "整")
        self.assertEqual(mimo.PROMPTS["half_hour"], "半")

    def test_fetch_decodes_documented_response_path_without_leaking_key(self) -> None:
        wav_data = make_wav()
        response = {
            "id": "response-id",
            "model": mimo.MODEL,
            "choices": [{
                "finish_reason": "stop",
                "message": {"audio": {"id": "audio-id", "data": base64.b64encode(wav_data).decode()}},
            }],
        }
        captured = {}

        def open_fn(request, timeout):
            captured["headers"] = dict(request.header_items())
            captured["body"] = json.loads(request.data)
            captured["timeout"] = timeout
            return FakeResponse(json.dumps(response).encode())

        stdout = io.StringIO()
        stderr = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            actual, metadata = mimo.fetch_audio(
                TEST_KEY, mimo.request_body("二"), 12.0, 1, open_fn=open_fn
            )
        self.assertEqual(actual, wav_data)
        self.assertEqual(metadata["audio_id"], "audio-id")
        self.assertEqual(captured["headers"]["Api-key"], TEST_KEY)
        self.assertNotIn("Authorization", captured["headers"])
        self.assertEqual(captured["timeout"], 12.0)
        self.assertNotIn(TEST_KEY, stdout.getvalue() + stderr.getvalue())

    def test_invalid_json_base64_and_missing_field_are_rejected(self) -> None:
        bad_responses = [
            b"not-json",
            b'{"choices":[]}',
            b'{"choices":[{"message":{"audio":{"data":"%%%"}}}]}',
        ]
        for raw in bad_responses:
            with self.subTest(raw=raw):
                with self.assertRaises(ValueError):
                    mimo.fetch_audio(
                        TEST_KEY,
                        mimo.request_body("二"),
                        1,
                        1,
                        open_fn=lambda _request, _timeout, raw=raw: FakeResponse(raw),
                    )

    def test_retryable_status_retries_but_400_does_not(self) -> None:
        calls = []
        wav_data = make_wav()
        success = json.dumps({
            "choices": [{"finish_reason": "stop", "message": {"audio": {"data": base64.b64encode(wav_data).decode()}}}]
        }).encode()

        def retry_open(_request, _timeout):
            calls.append(1)
            if len(calls) == 1:
                raise urllib.error.HTTPError(
                    mimo.PAY_AS_YOU_GO_ENDPOINT, 429, "rate", {}, None
                )
            return FakeResponse(success)

        sleeps = []
        actual, _ = mimo.fetch_audio(
            TEST_KEY, mimo.request_body("二"), 1, 2, retry_open, sleeps.append
        )
        self.assertEqual(actual, wav_data)
        self.assertEqual(len(calls), 2)
        self.assertEqual(sleeps, [1.0])

        with self.assertRaisesRegex(RuntimeError, "HTTP 400"):
            mimo.fetch_audio(
                TEST_KEY,
                mimo.request_body("二"),
                1,
                3,
                lambda _request, _timeout: (_ for _ in ()).throw(
                    urllib.error.HTTPError(
                        mimo.PAY_AS_YOU_GO_ENDPOINT, 400, "bad", {}, None
                    )
                ),
            )


class WavTests(unittest.TestCase):
    def test_valid_wav_metadata(self) -> None:
        metadata = mimo.parse_wav(make_wav())
        self.assertEqual(metadata["sample_rate"], 24000)
        self.assertEqual(metadata["channels"], 1)
        self.assertEqual(metadata["sample_width"], 2)
        self.assertEqual(metadata["full_scale_samples"], 0)

    def test_wrong_rate_channels_silence_and_clipping_fail(self) -> None:
        with self.assertRaises(ValueError):
            mimo.parse_wav(make_wav(rate=16000))
        with self.assertRaises(ValueError):
            mimo.parse_wav(make_wav(channels=2))

        def custom(samples):
            output = io.BytesIO()
            with wave.open(output, "wb") as target:
                target.setnchannels(1)
                target.setsampwidth(2)
                target.setframerate(24000)
                target.writeframes(struct.pack(f"<{len(samples)}h", *samples))
            return output.getvalue()

        with self.assertRaises(ValueError):
            mimo.parse_wav(custom([0] * 1000))
        with self.assertRaises(ValueError):
            mimo.parse_wav(custom([32767, -32768] * 1000))

    def test_atomic_output_permissions(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "voice.wav"
            mimo.write_atomic(target, b"data")
            self.assertEqual(target.read_bytes(), b"data")
            self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o600)


if __name__ == "__main__":
    unittest.main()
