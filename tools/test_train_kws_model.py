#!/usr/bin/env python3
"""Unit tests for KWS threshold selection and acceptance gates."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("train_kws_model.py")
SPEC = importlib.util.spec_from_file_location("train_kws_model", MODULE_PATH)
training = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = training
SPEC.loader.exec_module(training)


def prediction(actual: str, predicted: str, **scores: int) -> dict:
    raw = {label: -128 for label in training.MODEL_LABELS}
    raw.update(scores)
    return {
        "sample_id": f"{actual}-{predicted}-{len(scores)}",
        "actual": actual,
        "predicted": predicted,
        "scores_raw": raw,
    }


class ThresholdTests(unittest.TestCase):
    def test_thresholds_are_one_step_above_argmax_negatives(self) -> None:
        rows = [
            prediction("background", "background", query_temperature=-100),
            prediction("unknown_speech", "unknown_speech", query_temperature=-20),
            prediction("query_humidity", "query_humidity", query_humidity=80),
            prediction("query_light", "query_light", query_light=81),
            prediction("query_air_quality", "query_air_quality", query_air_quality=82),
            prediction("query_temperature", "query_temperature", query_temperature=50),
            prediction("wake_nihao_vela", "wake_nihao_vela", wake_nihao_vela=83),
        ]
        thresholds = training.choose_validation_thresholds(rows)
        self.assertEqual(thresholds["query_temperature"], -127)
        metrics = training.threshold_metrics(rows, thresholds)
        self.assertEqual(metrics["false_accepts"], 0)
        self.assertEqual(metrics["intent_recall"], 1.0)

    def test_threshold_reports_argmax_overlap_without_test_tuning(self) -> None:
        rows = [
            prediction("background", "background"),
            prediction("unknown_speech", "query_temperature", query_temperature=60),
            prediction("query_humidity", "query_humidity", query_humidity=80),
            prediction("query_light", "query_light", query_light=80),
            prediction("query_air_quality", "query_air_quality", query_air_quality=80),
            prediction("query_temperature", "query_temperature", query_temperature=50),
            prediction("wake_nihao_vela", "wake_nihao_vela", wake_nihao_vela=80),
        ]
        thresholds = training.choose_validation_thresholds(rows)
        self.assertEqual(thresholds["query_temperature"], 61)
        self.assertEqual(
            training.threshold_metrics(rows, thresholds)["per_label"]
            ["query_temperature"]["recall"],
            0.0,
        )

    def test_non_target_argmax_never_triggers_on_target_score_alone(self) -> None:
        rows = [
            prediction("background", "background"),
            prediction("unknown_speech", "unknown_speech", query_temperature=20),
            prediction("query_humidity", "query_humidity", query_humidity=80),
            prediction("query_light", "query_light", query_light=80),
            prediction("query_air_quality", "query_air_quality", query_air_quality=80),
            prediction("query_temperature", "query_temperature", query_temperature=80),
            prediction("wake_nihao_vela", "wake_nihao_vela", wake_nihao_vela=80),
        ]
        thresholds = {label: 0 for label in training.INTENT_LABELS}
        metrics = training.threshold_metrics(rows, thresholds)
        self.assertEqual(metrics["false_accepts"], 0)
        self.assertEqual(metrics["true_accepts"], len(training.INTENT_LABELS))

    def test_classification_confusion_is_label_complete(self) -> None:
        rows = [
            prediction("background", "background"),
            prediction("unknown_speech", "query_temperature"),
            prediction("query_temperature", "query_temperature"),
        ]
        metrics = training.classification_metrics(rows)
        self.assertAlmostEqual(metrics["accuracy"], 2 / 3)
        self.assertEqual(
            metrics["confusion"]["unknown_speech"]["query_temperature"], 1
        )
        self.assertEqual(set(metrics["confusion"]), set(training.LABELS))

    def test_gate_requires_quantization_and_both_held_out_splits(self) -> None:
        split = {
            "classification": {"accuracy": 1.0},
            "threshold": {"false_accepts": 0, "intent_recall": 1.0},
        }
        report = {
            "quantization": {"full_int8": True, "frontend_input_identity": True},
            "model": {"bytes": 10_000},
            "quantization_agreement": {
                "argmax_agreement": 1.0,
                "maximum_probability_delta": 0.01,
            },
            "evaluation": {"validation": split, "test": split},
            "timing_stress": {"validation": split, "test": split},
        }
        self.assertEqual(training.gate_report(report), (True, []))
        report["evaluation"]["test"]["threshold"]["false_accepts"] = 1
        accepted, reasons = training.gate_report(report)
        self.assertFalse(accepted)
        self.assertIn("test has an intent false accept", reasons)

    def test_gate_rejects_a_timing_stress_target_miss(self) -> None:
        split = {
            "classification": {"accuracy": 1.0},
            "threshold": {"false_accepts": 0, "intent_recall": 1.0},
        }
        timing_test = {
            "classification": {"accuracy": 0.95},
            "threshold": {"false_accepts": 0, "intent_recall": 0.8},
        }
        report = {
            "quantization": {"full_int8": True, "frontend_input_identity": True},
            "model": {"bytes": 10_000},
            "quantization_agreement": {
                "argmax_agreement": 1.0,
                "maximum_probability_delta": 0.01,
            },
            "evaluation": {"validation": split, "test": split},
            "timing_stress": {"validation": split, "test": timing_test},
        }
        accepted, reasons = training.gate_report(report)
        self.assertFalse(accepted)
        self.assertIn("timing test intent recall is below 85%", reasons)

    def test_pcm_time_shift_uses_quiet_fill_without_wrapping(self) -> None:
        import numpy as np

        samples = np.full(20_000, -100, dtype=np.int16)
        samples[8_000:10_000] = 800
        delayed = training.time_shift_samples(samples, 10, np)
        advanced = training.time_shift_samples(samples, -10, np)
        self.assertTrue(np.all(delayed[11_200:13_200] == 800))
        self.assertTrue(np.all(advanced[4_800:6_800] == 800))
        self.assertFalse(np.any(delayed[:3_200] == 800))
        self.assertFalse(np.any(advanced[-3_200:] == 800))

    def test_balanced_sample_weights_give_each_class_equal_total_weight(self) -> None:
        import numpy as np

        labels = np.array(
            [0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 6], dtype=np.int64
        )
        weights = training.balanced_sample_weights(labels, np)
        totals = [
            float(weights[labels == index].sum())
            for index in range(len(training.MODEL_LABELS))
        ]
        for total in totals[1:]:
            self.assertAlmostEqual(totals[0], total, places=6)

    def test_model_labels_keep_sensor_negatives_separate(self) -> None:
        self.assertEqual(
            training.model_label(
                {"training_class": "unknown", "label": "query_humidity"}
            ),
            "query_humidity",
        )
        self.assertEqual(training.evaluation_label("query_humidity"), "query_humidity")
        self.assertEqual(
            training.evaluation_label("query_temperature"), "query_temperature"
        )

    def test_temporal_warp_preserves_alignment_anchor(self) -> None:
        import numpy as np

        feature = np.full((178, 2), -128, dtype=np.int8)
        feature[training.ALIGNMENT_ANCHOR_FRAME] = [40, 80]
        warped = training.warp_feature_time(feature, 1.1, np)
        np.testing.assert_array_equal(
            warped[training.ALIGNMENT_ANCHOR_FRAME], [40, 80]
        )
        self.assertEqual(warped.shape, feature.shape)
        self.assertEqual(warped.dtype, np.int8)

    def test_validation_selector_prioritizes_threshold_separation(self) -> None:
        import numpy as np

        labels = np.array([0, 1, 2, 3, 4, 5, 6], dtype=np.int64)
        probabilities = np.array(
            [
                [0.8, 0.1, 0.0, 0.0, 0.0, 0.1, 0.0],
                [0.1, 0.8, 0.0, 0.0, 0.0, 0.1, 0.0],
                [0.0, 0.0, 0.9, 0.0, 0.0, 0.1, 0.0],
                [0.0, 0.0, 0.0, 0.9, 0.0, 0.1, 0.0],
                [0.0, 0.0, 0.0, 0.0, 0.9, 0.1, 0.0],
                [0.0, 0.0, 0.0, 0.0, 0.1, 0.9, 0.0],
                [0.0, 0.0, 0.0, 0.0, 0.0, 0.1, 0.9],
            ],
            dtype=np.float32,
        )
        metrics = training.validation_selection_metrics(
            labels, probabilities, np
        )
        self.assertEqual(metrics["accepted_intents"], 5)
        self.assertEqual(metrics["intent_argmax"], 5)
        self.assertEqual(metrics["intent_false_argmax"], 0)
        self.assertGreater(metrics["minimum_intent_margin"], 0.0)

    def test_timing_stress_anchors_detected_speech_at_requested_frames(self) -> None:
        import numpy as np

        class Frontend:
            @staticmethod
            def align(samples):
                nonzero = np.flatnonzero(samples)
                return samples, None if nonzero.size == 0 else int(nonzero[0])

            @staticmethod
            def extract_aligned(samples):
                return samples.reshape(160, 40).astype(np.int8), None

        sample = np.zeros(6_400, dtype=np.int16)
        sample[640:960] = 10
        rows = [{"sample_id": "sample"}]
        original = training.TIMING_STRESS_ONSET_FRAMES
        try:
            training.TIMING_STRESS_ONSET_FRAMES = (0, 1)
            features, stress_rows = training.timing_stress_set(
                sample[np.newaxis], rows, Frontend(), np
            )
        finally:
            training.TIMING_STRESS_ONSET_FRAMES = original
        self.assertEqual(features.shape, (2, 160, 40))
        self.assertEqual(
            [row["timing_onset_frame"] for row in stress_rows], [0, 1]
        )


if __name__ == "__main__":
    unittest.main()
