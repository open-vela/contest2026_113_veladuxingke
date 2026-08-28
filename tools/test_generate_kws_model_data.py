#!/usr/bin/env python3
"""Tests for the accepted-model-to-C++ generation gate."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("generate_kws_model_data.py")
SPEC = importlib.util.spec_from_file_location("generate_kws_model_data", MODULE_PATH)
generator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = generator
SPEC.loader.exec_module(generator)


class ModelDataGateTests(unittest.TestCase):
    def write_fixture(self, root: Path, accepted: bool = True) -> bytes:
        model = bytes(index % 251 for index in range(65_496))
        evaluation = {
            "schema_version": 1,
            "accepted": accepted,
            "model": {"sha256": generator.sha256(model), "bytes": len(model)},
            "model_labels": list(generator.MODEL_LABELS),
            "thresholds": {
                "by_label": {
                    label: {"raw_int8": -105}
                    for label in generator.INTENT_LABELS
                }
            },
            "quantization": {
                "full_int8": True,
                "frontend_input_identity": True,
            },
        }
        evaluation_data = (
            json.dumps(evaluation, indent=2, sort_keys=True) + "\n"
        ).encode()
        acceptance = {
            "schema_version": 1,
            "model_sha256": generator.sha256(model),
            "evaluation_sha256": generator.sha256(evaluation_data),
            "thresholds_raw_int8": {
                label: -105 for label in generator.INTENT_LABELS
            },
        }
        (root / "kws_int8.tflite").write_bytes(model)
        (root / "evaluation.json").write_bytes(evaluation_data)
        (root / "MODEL_ACCEPTED.json").write_text(
            json.dumps(acceptance), encoding="utf-8"
        )
        return model

    def test_generation_preserves_model_bytes_and_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            model = self.write_fixture(root)
            header = root / "kws_model_data.h"
            source = root / "kws_model_data.cc"
            details = generator.generate(root, header, source)
            text = source.read_text()
            rendered = bytes(
                int(token, 16)
                for token in __import__("re").findall(r"0x([0-9a-f]{2})", text)
            )
            self.assertEqual(rendered, model)
            self.assertEqual(details["model_sha256"], generator.sha256(model))
            self.assertIn("R528_KWS_MODEL_EXPECTED_BYTES 65496", header.read_text())
            self.assertIn("R528_KWS_CATEGORY_COUNT 7", header.read_text())
            self.assertIn("R528_KWS_TEMPERATURE_INDEX 5", header.read_text())
            self.assertIn("R528_KWS_WAKE_INDEX 6", header.read_text())
            self.assertIn(
                "R528_KWS_TEMPERATURE_THRESHOLD (-105)", header.read_text()
            )

    def test_rejected_or_tampered_model_is_never_generated(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.write_fixture(root, accepted=False)
            with self.assertRaisesRegex(ValueError, "did not accept"):
                generator.generate(root, root / "out.h", root / "out.cc")

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.write_fixture(root)
            (root / "kws_int8.tflite").write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                generator.generate(root, root / "out.h", root / "out.cc")


if __name__ == "__main__":
    unittest.main()
