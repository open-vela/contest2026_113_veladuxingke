#!/usr/bin/env python3
"""Tests for deterministic R528 KWS corpus import."""

import importlib.util
import io
import json
import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("import_kws_corpus.py")
SPEC = importlib.util.spec_from_file_location("import_kws_corpus", MODULE_PATH)
corpus = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = corpus
SPEC.loader.exec_module(corpus)


def write_sample(root: Path, sample_id: str, label: str, samples: list[int],
                 requested_ms: int | None = None) -> tuple[Path, Path]:
    pcm = struct.pack(f"<{len(samples)}h", *samples)
    requested_ms = requested_ms or round(len(samples) * 1000 / 16000)
    match = __import__("re").fullmatch(corpus.load_spec()["id"]["sample_pattern"], sample_id)
    if match is None:
        raise ValueError(f"invalid test sample ID: {sample_id}")
    metadata = {
        "schema_version": 1,
        "sample_id": sample_id,
        "device_id": "r528a",
        "speaker_id": "s001",
        "session_id": "session_a",
        "label": label,
        "take": int(match.group("take")),
        "capture_device": "/dev/audio/pcm1c",
        "encoding": "pcm_s16le",
        "sample_rate_hz": 16000,
        "channels": 1,
        "bits_per_sample": 16,
        "requested_ms": requested_ms,
        "actual_bytes": len(pcm),
        "frames": len(samples),
        "duration_ms": len(samples) * 1000 / 16000,
        "status": "complete",
    }
    pcm_path = root / f"{sample_id}.pcm"
    json_path = root / f"{sample_id}.json"
    pcm_path.write_bytes(pcm)
    json_path.write_text(json.dumps(metadata), encoding="utf-8")
    return pcm_path, json_path


class MetricTests(unittest.TestCase):
    def test_known_metrics_and_wav_payload(self) -> None:
        spec = corpus.load_spec()
        pcm = struct.pack("<4h", 1000, -1000, 1000, -1000)
        metrics = corpus.measure_pcm(pcm, spec)
        self.assertEqual(metrics["peak"], 1000)
        self.assertEqual(metrics["rms"], 1000)
        self.assertEqual(metrics["dc_mean"], 0)
        self.assertEqual(metrics["clipped_samples"], 0)
        wav_data = corpus.make_wav(pcm, spec)
        with wave.open(io.BytesIO(wav_data), "rb") as source:
            self.assertEqual(source.getframerate(), 16000)
            self.assertEqual(source.getnchannels(), 1)
            self.assertEqual(source.getsampwidth(), 2)
            self.assertEqual(source.readframes(source.getnframes()), pcm)

    def test_empty_and_odd_pcm_fail(self) -> None:
        spec = corpus.load_spec()
        for value in (b"", b"\x00"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                corpus.measure_pcm(value, spec)


class ValidationTests(unittest.TestCase):
    def test_valid_sample_and_quality_warning(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sample_id = "k0123456789abcdef_qt_0001"
            write_sample(root, sample_id, "query_temperature", [0] * 16000, 1000)
            samples = corpus.validate_staging(root, corpus.load_spec())
            self.assertEqual(samples[0].sample_id, sample_id)
            self.assertIn("all_zero", samples[0].warnings)
            self.assertIn("low_positive_activity", samples[0].warnings)

    def test_unpaired_part_symlink_and_metadata_mismatch_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "left.pcm").write_bytes(b"\0\0")
            with self.assertRaises(ValueError):
                corpus.validate_staging(root, corpus.load_spec())
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "sample.part").write_bytes(b"x")
            with self.assertRaises(ValueError):
                corpus.validate_staging(root, corpus.load_spec())
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target = root / "target"
            target.write_bytes(b"x")
            (root / "link.pcm").symlink_to(target)
            with self.assertRaises(ValueError):
                corpus.validate_staging(root, corpus.load_spec())
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sample_id = "k0123456789abcdef_bg_0001"
            _, metadata = write_sample(root, sample_id, "background", [1] * 16000, 1000)
            value = json.loads(metadata.read_text())
            value["actual_bytes"] += 2
            metadata.write_text(json.dumps(value))
            with self.assertRaises(ValueError):
                corpus.validate_staging(root, corpus.load_spec())


class ImportTests(unittest.TestCase):
    def test_session_relabel_is_deterministic_and_preserves_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            staging = root / "staging"
            output = root / "output"
            staging.mkdir()
            source_id = "k0123456789abcdef_us_0001"
            _, metadata_path = write_sample(
                staging,
                source_id,
                "unknown_speech",
                [1200, -1200] * 8000,
                1000,
            )
            metadata = json.loads(metadata_path.read_text())
            metadata["session_id"] = "wake01"
            metadata_path.write_text(json.dumps(metadata))

            result = corpus.import_corpus(
                staging,
                output,
                session_relabels={"wake01": "wake_nihao_vela"},
            )
            target_id = "k0123456789abcdef_wv_0001"
            imported = json.loads(
                (output / "metadata/wake_nihao_vela" / f"{target_id}.json")
                .read_text()
            )
            self.assertEqual(result["imported_this_run"], 1)
            self.assertEqual(imported["sample_id"], target_id)
            self.assertEqual(imported["label"], "wake_nihao_vela")
            self.assertEqual(
                imported["relabelled_from"],
                {"sample_id": source_id, "label": "unknown_speech"},
            )
            self.assertTrue((staging / f"{source_id}.pcm").exists())

    def test_import_is_deterministic_and_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            staging = root / "staging"
            output = root / "output"
            staging.mkdir()
            sample_id = "k0123456789abcdef_qt_0001"
            write_sample(staging, sample_id, "query_temperature", [1200, -1200] * 8000, 1000)
            first = corpus.import_corpus(staging, output)
            first_manifest = (output / "manifest.jsonl").read_bytes()
            first_wav = (output / "wav/query_temperature" / f"{sample_id}.wav").read_bytes()
            second = corpus.import_corpus(staging, output)
            self.assertEqual(first["imported_this_run"], 1)
            self.assertEqual(second["imported_this_run"], 0)
            self.assertEqual((output / "manifest.jsonl").read_bytes(), first_manifest)
            self.assertEqual(
                (output / "wav/query_temperature" / f"{sample_id}.wav").read_bytes(),
                first_wav,
            )

    def test_conflicting_existing_sample_fails_without_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            staging = root / "staging"
            output = root / "output"
            staging.mkdir()
            sample_id = "k0123456789abcdef_bg_0001"
            write_sample(staging, sample_id, "background", [100, -100] * 8000, 1000)
            corpus.import_corpus(staging, output)
            pcm = output / "raw/background" / f"{sample_id}.pcm"
            pcm.write_bytes(b"conflict")
            with self.assertRaisesRegex(ValueError, "conflicting"):
                corpus.import_corpus(staging, output)
            self.assertEqual(pcm.read_bytes(), b"conflict")

    def test_repository_output_requires_explicit_override(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            staging = Path(temp)
            sample_id = "k0123456789abcdef_bg_0001"
            write_sample(staging, sample_id, "background", [1] * 16000, 1000)
            with self.assertRaisesRegex(ValueError, "repository"):
                corpus.import_corpus(staging, corpus.PROJECT_ROOT / "private-corpus")


if __name__ == "__main__":
    unittest.main()
