#!/usr/bin/env python3
"""Tests for the fixed-point KWS frontend bridge and Python wrapper."""

from __future__ import annotations

import importlib.util
import os
import sys
import unittest
from pathlib import Path

try:
    import numpy as np
except ModuleNotFoundError:
    np = None

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parents[1]
FRONTEND = ROOT / "contest2026_113_veladuxingke/app/routine_manager/kws_frontend.cc"
frontend_module = None
if np is not None:
    MODULE_SPEC = importlib.util.spec_from_file_location(
        "kws_microfrontend", TOOLS / "kws_microfrontend.py"
    )
    frontend_module = importlib.util.module_from_spec(MODULE_SPEC)
    sys.modules[MODULE_SPEC.name] = frontend_module
    MODULE_SPEC.loader.exec_module(frontend_module)


class MicrofrontendContractTests(unittest.TestCase):
    def test_bridge_configuration_matches_micro_speech(self) -> None:
        source = FRONTEND.read_text()
        for expected in (
            "kSampleRate = 16000",
            "kWindowSamples = 480",
            "kStrideSamples = 320",
            "kFeatureChannels = 40",
            "lower_band_limit = 125.0f",
            "upper_band_limit = 7500.0f",
            "enable_pcan = 1",
            "scale_shift = 6",
            "kFeatureValueDivisor = 666",
            "kVadSearchStartSamples = 0",
            "kVadRequiredActiveFrames = 3",
            "kAlignmentAnchorSamples = 9600",
            "kAlignmentPrerollSamples = 1600",
            "r528_kws_align_audio",
        ):
            self.assertIn(expected, source)

    def test_build_uses_openvela_kissfft_and_fixed_source_list(self) -> None:
        source = (TOOLS / "build_kws_microfrontend.py").read_text()
        self.assertIn('ROOT / "apps/math/kissfft/kissfft"', source)
        self.assertIn('"kiss_fft_int16.cc"', source)
        self.assertIn('"frontend.c"', source)


@unittest.skipUnless(
    np is not None and os.environ.get("R528_KWS_FRONTEND_LIBRARY"),
    "NumPy and R528_KWS_FRONTEND_LIBRARY are required for compiled tests",
)
class CompiledMicrofrontendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.frontend = frontend_module.Microfrontend(
            Path(os.environ["R528_KWS_FRONTEND_LIBRARY"])
        )

    def test_dimensions_and_short_input(self) -> None:
        self.assertEqual(self.frontend.feature_frames(479), 0)
        self.assertEqual(self.frontend.feature_frames(480), 1)
        self.assertEqual(self.frontend.feature_frames(57_344), 178)
        with self.assertRaisesRegex(ValueError, "shorter"):
            self.frontend.extract(np.zeros(479, dtype=np.int16))

    def test_alignment_is_speech_onset_invariant(self) -> None:
        immediate = np.zeros(57_344, dtype=np.int16)
        early = np.zeros(57_344, dtype=np.int16)
        late = np.zeros(57_344, dtype=np.int16)
        phrase = np.resize(
            np.array([1200, -1200, 800, -800], dtype=np.int16), 16_000
        )
        immediate[:16_000] = phrase
        early[8_000:24_000] = phrase
        late[24_000:40_000] = phrase
        aligned_immediate, onset_immediate = self.frontend.align(immediate)
        aligned_early, onset_early = self.frontend.align(early)
        aligned_late, onset_late = self.frontend.align(late)
        self.assertEqual(onset_immediate, 0)
        self.assertEqual(onset_early, 8_000)
        self.assertEqual(onset_late, 24_000)
        np.testing.assert_array_equal(aligned_immediate, aligned_early)
        np.testing.assert_array_equal(aligned_early, aligned_late)

    def test_alignment_rejects_silence(self) -> None:
        aligned, onset = self.frontend.align(
            np.zeros(57_344, dtype=np.int16)
        )
        self.assertIsNone(onset)
        self.assertFalse(np.any(aligned))

        aligned, onset = self.frontend.align(
            np.full(57_344, -4_000, dtype=np.int16)
        )
        self.assertIsNone(onset)
        self.assertFalse(np.any(aligned))

    def test_alignment_ignores_capture_start_dc_transient(self) -> None:
        samples = np.zeros(57_344, dtype=np.int16)
        samples[:960] = np.repeat(
            np.array([-4000, 1000, 800], dtype=np.int16), 320
        )
        phrase = np.resize(
            np.array([1200, -1200, 800, -800], dtype=np.int16), 16_000
        )
        samples[3_200:19_200] = phrase
        _, onset = self.frontend.align(samples)
        self.assertEqual(onset, 3_200)

    def test_upstream_yes_reference_is_exact(self) -> None:
        wav = (
            ROOT
            / "apps/mlearning/tflite-micro/tflite-micro/tensorflow/lite/micro"
            / "examples/micro_speech/testdata/yes_30ms.wav"
        )
        expected = np.array(
            [
                124, 105, 126, 103, 125, 101, 123, 100, 116, 98,
                115, 97, 113, 90, 91, 82, 104, 96, 117, 97,
                121, 103, 126, 101, 125, 104, 126, 104, 125, 101,
                116, 90, 81, 74, 80, 71, 83, 76, 82, 71,
            ],
            dtype=np.int8,
        )
        np.testing.assert_array_equal(self.frontend.extract_wav(wav)[0], expected)

    def test_openvela_kissfft_no_reference_rounding_is_bounded(self) -> None:
        wav = (
            ROOT
            / "apps/mlearning/tflite-micro/tflite-micro/tensorflow/lite/micro"
            / "examples/micro_speech/testdata/no_30ms.wav"
        )
        upstream = np.array(
            [
                126, 103, 124, 102, 124, 102, 123, 100, 118, 97,
                118, 100, 118, 98, 121, 100, 121, 98, 117, 91,
                96, 74, 54, 87, 100, 87, 109, 92, 91, 80,
                64, 55, 83, 74, 74, 78, 114, 95, 101, 81,
            ],
            dtype=np.int16,
        )
        actual = self.frontend.extract_wav(wav)[0].astype(np.int16)
        delta = np.abs(actual - upstream)
        self.assertLessEqual(int(delta.max()), 1)
        self.assertEqual(int(np.count_nonzero(delta)), 3)


if __name__ == "__main__":
    unittest.main()
