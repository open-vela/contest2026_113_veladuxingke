#!/usr/bin/env python3
"""Offline tests for the optional MiMo V2.5 ASR baseline."""

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
from unittest import mock

TOOLS = Path(__file__).parent
import sys
sys.path.insert(0, str(TOOLS))

MODULE_PATH = TOOLS / "mimo_asr_baseline.py"
SPEC = importlib.util.spec_from_file_location("mimo_asr_baseline", MODULE_PATH)
mimo = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mimo)

LAUNCHER_PATH = TOOLS / "run_mimo_asr_baseline.py"
LAUNCHER_SPEC = importlib.util.spec_from_file_location("run_mimo_asr_baseline", LAUNCHER_PATH)
launcher = importlib.util.module_from_spec(LAUNCHER_SPEC)
LAUNCHER_SPEC.loader.exec_module(launcher)

TEST_KEY = "mimo-test-secret-never-print"


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


def make_wav() -> bytes:
    output = io.BytesIO()
    with wave.open(output, "wb") as target:
        target.setnchannels(1)
        target.setsampwidth(2)
        target.setframerate(16000)
        target.writeframes(struct.pack("<320h", *([1200, -1200] * 160)))
    return output.getvalue()


class RequestTests(unittest.TestCase):
    def test_request_matches_documented_schema(self) -> None:
        body = mimo.request_body(make_wav())
        self.assertEqual(body["model"], "mimo-v2.5-asr")
        self.assertEqual(body["asr_options"], {"language": "zh"})
        self.assertIs(body["stream"], False)
        audio = body["messages"][0]["content"][0]
        self.assertEqual(audio["type"], "input_audio")
        self.assertTrue(audio["input_audio"]["data"].startswith("data:audio/wav;base64,"))

    def test_response_uses_exact_content_path_without_key_leak(self) -> None:
        response = {
            "id": "response-id",
            "model": mimo.MODEL,
            "choices": [{
                "finish_reason": "stop",
                "message": {"content": "现在温度多少。"},
            }],
        }
        captured = {}

        def open_fn(request, timeout):
            captured["headers"] = dict(request.header_items())
            captured["body"] = json.loads(request.data)
            return FakeResponse(json.dumps(response).encode())

        stdout = io.StringIO()
        stderr = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            transcript, metadata = mimo.fetch_transcript(
                TEST_KEY, mimo.request_body(make_wav()), 2, 1, open_fn=open_fn
            )
        self.assertEqual(transcript, "现在温度多少。")
        self.assertEqual(metadata["response_id"], "response-id")
        self.assertEqual(captured["headers"]["Api-key"], TEST_KEY)
        self.assertNotIn(TEST_KEY, stdout.getvalue() + stderr.getvalue())

    def test_missing_empty_or_wrong_content_fails(self) -> None:
        responses = [
            b"not-json",
            b'{"choices":[]}',
            b'{"choices":[{"message":{"content":""}}]}',
            b'{"choices":[{"message":{"content":7}}]}',
        ]
        for raw in responses:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                mimo.fetch_transcript(
                    TEST_KEY,
                    mimo.request_body(make_wav()),
                    1,
                    1,
                    open_fn=lambda _request, _timeout, raw=raw: FakeResponse(raw),
                )

    def test_retry_policy(self) -> None:
        calls = []
        success = b'{"choices":[{"message":{"content":"test"}}]}'

        def retry_open(_request, _timeout):
            calls.append(1)
            if len(calls) == 1:
                raise urllib.error.HTTPError(mimo.PAY_AS_YOU_GO_ENDPOINT, 429, "rate", {}, None)
            return FakeResponse(success)

        sleeps = []
        mimo.fetch_transcript(
            TEST_KEY, mimo.request_body(make_wav()), 1, 2, retry_open, sleeps.append
        )
        self.assertEqual(sleeps, [1.0])
        with self.assertRaisesRegex(RuntimeError, "HTTP 400"):
            mimo.fetch_transcript(
                TEST_KEY,
                mimo.request_body(make_wav()),
                1,
                3,
                lambda *_args: (_ for _ in ()).throw(
                    urllib.error.HTTPError(mimo.PAY_AS_YOU_GO_ENDPOINT, 400, "bad", {}, None)
                ),
            )


class SecurityAndNormalizationTests(unittest.TestCase):
    def test_key_file_permissions_and_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            key = root / "key"
            key.write_text(TEST_KEY)
            key.chmod(0o600)
            self.assertEqual(mimo.read_key_file(key), TEST_KEY)
            key.chmod(0o644)
            with self.assertRaises(ValueError):
                mimo.read_key_file(key)
            key.chmod(0o600)
            link = root / "link"
            link.symlink_to(key)
            with self.assertRaises(ValueError):
                mimo.read_key_file(link)

    def test_normalization_is_minimal_and_deterministic(self) -> None:
        self.assertEqual(mimo.normalize_transcript(" 现在，温度多少？ "), "现在温度多少")
        self.assertEqual(mimo.normalize_transcript("ＡＢ C"), "abc")
        self.assertEqual(mimo.normalize_transcript("二十四"), "二十四")
        phrases = mimo.phrase_index(mimo.load_spec())
        self.assertEqual(phrases["现在温度多少"], "query_temperature")
        self.assertEqual(phrases["现在温度是多少"], "query_temperature")
        self.assertEqual(phrases["现在湿度是多少"], "query_humidity")
        self.assertEqual(phrases["当前湿度多少"], "query_humidity")
        self.assertEqual(phrases["现在光照是多少"], "query_light")

    def test_launcher_requires_exact_upload_consent(self) -> None:
        with mock.patch("builtins.input", return_value="yes"):
            self.assertFalse(launcher.prompt_for_consent(["sample"]))
        with mock.patch("builtins.input", return_value="UPLOAD"):
            self.assertTrue(launcher.prompt_for_consent(["sample"]))

    def test_endpoint_matches_key_type(self) -> None:
        self.assertEqual(mimo.endpoint_for_key("tp-example")[1], "token-plan")
        self.assertEqual(mimo.endpoint_for_key("sk-example")[1], "pay-as-you-go")


class BaselineTests(unittest.TestCase):
    def test_selected_samples_only_and_human_label_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            corpus = root / "corpus"
            wav_dir = corpus / "wav/query_temperature"
            wav_dir.mkdir(parents=True)
            wav_data = make_wav()
            sample_id = "k0123456789abcdef_qt_0001"
            wav_path = wav_dir / f"{sample_id}.wav"
            wav_path.write_bytes(wav_data)
            entry = {
                "sample_id": sample_id,
                "label": "query_temperature",
                "wav_sha256": mimo.sha256(wav_data),
            }
            (corpus / "manifest.jsonl").write_bytes(mimo.canonical_json(entry) + b"\n")
            key = root / "key"
            key.write_text(TEST_KEY)
            key.chmod(0o600)

            def fetch(_key, _body, _timeout, _attempts, endpoint=None):
                return "现在湿度多少", {
                    "response_id": "id",
                    "response_model": mimo.MODEL,
                    "finish_reason": "stop",
                }

            summary = mimo.run_baseline(key, corpus, [sample_id], fetch_fn=fetch)
            self.assertEqual(summary["needs_review"], 1)
            record = json.loads((corpus / "baseline" / f"{mimo.MODEL}.jsonl").read_text())
            self.assertEqual(record["human_label"], "query_temperature")
            self.assertEqual(record["predicted_label"], "query_humidity")
            self.assertTrue(record["needs_review"])

    def test_existing_results_are_merged_and_skipped(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            corpus = root / "corpus"
            wav_dir = corpus / "wav/query_temperature"
            wav_dir.mkdir(parents=True)
            wav_data = make_wav()
            sample_ids = [
                "k0123456789abcdef_qt_0001",
                "k0123456789abcdef_qt_0002",
            ]
            entries = []
            for sample_id in sample_ids:
                (wav_dir / f"{sample_id}.wav").write_bytes(wav_data)
                entries.append({
                    "sample_id": sample_id,
                    "label": "query_temperature",
                    "wav_sha256": mimo.sha256(wav_data),
                })
            (corpus / "manifest.jsonl").write_bytes(
                b"".join(mimo.canonical_json(entry) + b"\n" for entry in entries)
            )
            baseline = corpus / "baseline"
            baseline.mkdir()
            existing = {
                "schema_version": 1,
                "sample_id": sample_ids[0],
                "human_label": "query_temperature",
                "transcript": "现在温度是多少？",
                "normalized_transcript": "stale",
                "predicted_label": "unknown_speech",
                "needs_review": True,
                "model": mimo.MODEL,
                "response_id": "old",
                "response_model": mimo.MODEL,
                "finish_reason": "stop",
            }
            (baseline / f"{mimo.MODEL}.jsonl").write_bytes(
                mimo.canonical_json(existing) + b"\n"
            )
            key = root / "key"
            key.write_text(TEST_KEY)
            key.chmod(0o600)
            fetched = []

            def fetch(_key, _body, _timeout, _attempts, endpoint=None):
                fetched.append(1)
                return "现在温度多少", {
                    "response_id": "new",
                    "response_model": mimo.MODEL,
                    "finish_reason": "stop",
                }

            summary = mimo.run_baseline(key, corpus, sample_ids, fetch_fn=fetch)
            records = [
                json.loads(line)
                for line in (baseline / f"{mimo.MODEL}.jsonl").read_text().splitlines()
            ]
            self.assertEqual(len(fetched), 1)
            self.assertEqual(len(records), 2)
            self.assertFalse(records[0]["needs_review"])
            self.assertEqual(summary["processed_this_run"], 1)
            self.assertEqual(summary["skipped_existing"], 1)

    def test_success_is_checkpointed_before_later_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            corpus = root / "corpus"
            wav_dir = corpus / "wav/query_temperature"
            wav_dir.mkdir(parents=True)
            wav_data = make_wav()
            sample_ids = [
                "k0123456789abcdef_qt_0001",
                "k0123456789abcdef_qt_0002",
            ]
            entries = []
            for sample_id in sample_ids:
                (wav_dir / f"{sample_id}.wav").write_bytes(wav_data)
                entries.append({
                    "sample_id": sample_id,
                    "label": "query_temperature",
                    "wav_sha256": mimo.sha256(wav_data),
                })
            (corpus / "manifest.jsonl").write_bytes(
                b"".join(mimo.canonical_json(entry) + b"\n" for entry in entries)
            )
            key = root / "key"
            key.write_text(TEST_KEY)
            key.chmod(0o600)
            calls = []

            def fetch(_key, _body, _timeout, _attempts, endpoint=None):
                calls.append(1)
                if len(calls) == 2:
                    raise RuntimeError("later request failed")
                return "现在温度多少", {
                    "response_id": "new",
                    "response_model": mimo.MODEL,
                    "finish_reason": "stop",
                }

            with self.assertRaisesRegex(RuntimeError, "later request failed"):
                mimo.run_baseline(key, corpus, sample_ids, fetch_fn=fetch)
            records = (
                corpus / "baseline" / f"{mimo.MODEL}.jsonl"
            ).read_text().splitlines()
            self.assertEqual(len(records), 1)
            self.assertEqual(json.loads(records[0])["sample_id"], sample_ids[0])


if __name__ == "__main__":
    unittest.main()
