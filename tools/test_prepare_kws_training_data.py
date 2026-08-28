#!/usr/bin/env python3
"""Tests for curated, deterministic R528 KWS training manifests."""

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("prepare_kws_training_data.py")
SPEC = importlib.util.spec_from_file_location("prepare_kws_training_data", MODULE_PATH)
training = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = training
SPEC.loader.exec_module(training)


def write_corpus(root: Path, counts: dict[str, int]) -> list[dict]:
    class_by_label = {
        "query_temperature": "target",
        "query_humidity": "unknown",
        "background": "background",
    }
    code_by_label = {
        "query_temperature": "qt",
        "query_humidity": "qh",
        "background": "bg",
    }
    rows = []
    context = 1
    for label, count in counts.items():
        for take in range(1, count + 1):
            sample_id = f"k{context:016x}_{code_by_label[label]}_{take:04d}"
            wav_data = b"RIFF" + sample_id.encode("ascii")
            metadata = {
                "schema_version": 1,
                "sample_id": sample_id,
                "label": label,
                "speaker_id": "s001",
                "session_id": "session_a",
                "device_id": "r528a",
            }
            wav = root / "wav" / label / f"{sample_id}.wav"
            sidecar = root / "metadata" / label / f"{sample_id}.json"
            wav.parent.mkdir(parents=True, exist_ok=True)
            sidecar.parent.mkdir(parents=True, exist_ok=True)
            wav.write_bytes(wav_data)
            sidecar.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
            rows.append(
                {
                    "schema_version": 1,
                    "sample_id": sample_id,
                    "label": label,
                    "training_class": class_by_label[label],
                    "pcm_sha256": "0" * 64,
                    "wav_sha256": training.sha256(wav_data),
                    "metadata_sha256": training.sha256(
                        training.canonical_json(metadata)
                    ),
                    "metrics": {},
                    "warnings": [],
                }
            )
        context += 1
    rows.sort(key=lambda row: row["sample_id"])
    (root / "manifest.jsonl").write_bytes(
        b"".join(training.canonical_json(row) + b"\n" for row in rows)
    )
    return rows


def curation_record(sample_id: str, decision: str) -> dict:
    return {
        "schema_version": 1,
        "sample_id": sample_id,
        "decision": decision,
        "reason": "test_review",
        "review_source": "user",
        "reviewed_on": "2026-08-26",
    }


class TrainingManifestTests(unittest.TestCase):
    def test_curation_filter_and_stratified_splits_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            rows = write_corpus(
                root,
                {"query_temperature": 7, "query_humidity": 7, "background": 4},
            )
            include_id = rows[0]["sample_id"]
            exclude_id = next(
                row["sample_id"]
                for row in rows
                if row["label"] == "query_humidity"
            )
            decisions = [
                curation_record(include_id, "include"),
                curation_record(exclude_id, "exclude"),
            ]
            (root / "curation.jsonl").write_bytes(
                b"".join(training.canonical_json(row) + b"\n" for row in decisions)
            )
            first = training.prepare_training_data(root)
            first_data = (root / "training/selection.jsonl").read_bytes()
            second = training.prepare_training_data(root)
            self.assertEqual(first, second)
            self.assertEqual(first_data, (root / "training/selection.jsonl").read_bytes())
            selected = [json.loads(line) for line in first_data.splitlines()]
            selected_ids = {row["sample_id"] for row in selected}
            self.assertIn(include_id, selected_ids)
            self.assertNotIn(exclude_id, selected_ids)
            self.assertEqual(first["source_samples"], 18)
            self.assertEqual(first["selected_samples"], 17)
            self.assertEqual(first["splits"], {"train": 11, "validation": 3, "test": 3})
            for label in {row["label"] for row in selected}:
                self.assertEqual(
                    {row["split"] for row in selected if row["label"] == label},
                    {"train", "validation", "test"},
                )

    def test_quality_warning_is_excluded(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            rows = write_corpus(root, {"query_temperature": 3})
            manifest = [
                json.loads(line)
                for line in (root / "manifest.jsonl").read_text().splitlines()
            ]
            manifest[0]["warnings"] = ["all_zero"]
            (root / "manifest.jsonl").write_bytes(
                b"".join(training.canonical_json(row) + b"\n" for row in manifest)
            )
            summary = training.prepare_training_data(root)
            self.assertEqual(summary["selected_samples"], 2)
            self.assertEqual(summary["excluded_by_warning"], [rows[0]["sample_id"]])

    def test_unknown_duplicate_and_invalid_curation_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            rows = write_corpus(root, {"query_temperature": 3})
            duplicate = curation_record(rows[0]["sample_id"], "exclude")
            (root / "curation.jsonl").write_bytes(
                training.canonical_json(duplicate)
                + b"\n"
                + training.canonical_json(duplicate)
                + b"\n"
            )
            with self.assertRaisesRegex(ValueError, "duplicate"):
                training.prepare_training_data(root)

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_corpus(root, {"query_temperature": 3})
            unknown = curation_record("kffffffffffffffff_qt_9999", "exclude")
            (root / "curation.jsonl").write_bytes(
                training.canonical_json(unknown) + b"\n"
            )
            with self.assertRaisesRegex(ValueError, "unknown"):
                training.prepare_training_data(root)

    def test_tampered_wav_fails_without_replacing_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            rows = write_corpus(root, {"query_temperature": 3})
            training.prepare_training_data(root)
            selection = root / "training/selection.jsonl"
            before = selection.read_bytes()
            sample_id = rows[0]["sample_id"]
            (root / "wav/query_temperature" / f"{sample_id}.wav").write_bytes(
                b"tampered"
            )
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                training.prepare_training_data(root)
            self.assertEqual(selection.read_bytes(), before)

    def test_output_must_remain_under_corpus(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write_corpus(root, {"query_temperature": 3})
            with self.assertRaisesRegex(ValueError, "subdirectory"):
                training.prepare_training_data(root, root.parent / "elsewhere")


if __name__ == "__main__":
    unittest.main()
