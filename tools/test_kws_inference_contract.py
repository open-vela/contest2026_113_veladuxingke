#!/usr/bin/env python3
"""Source and generated-asset contracts for R528 offline KWS inference."""

from __future__ import annotations

import hashlib
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OPENVELA = ROOT.parent
APP = ROOT / "app/routine_manager"
KWS = (APP / "voice_recognizer_kws.cc").read_text()
FRONTEND = (APP / "kws_frontend.cc").read_text()
MODEL_HEADER = (APP / "kws_model_data.h").read_text()
MODEL_SOURCE = (APP / "kws_model_data.cc").read_text()
KCONFIG = (APP / "Kconfig").read_text()
MAKEFILE = (APP / "Makefile").read_text()
CMAKE = (APP / "CMakeLists.txt").read_text()
DEFCONFIG = (ROOT / "board/r528s3-dshanpi/configs/nsh/defconfig").read_text()
TFLM_MAKE = (OPENVELA / "apps/mlearning/tflite-micro/Makefile").read_text()
TFLM_CMAKE = (OPENVELA / "apps/mlearning/tflite-micro/CMakeLists.txt").read_text()
BUILD_FRONTEND = (ROOT / "tools/build_kws_microfrontend.py").read_text()
BOARD_BRINGUP = (
    ROOT / "board/r528s3-dshanpi/src/r528_bringup.c"
).read_text()
CHIP_BOOT = (OPENVELA / "vendor/allwinnertech/chips/r528/r528_boot.c").read_text()


class KwsInferenceContracts(unittest.TestCase):
    def test_generated_model_bytes_match_accepted_sha_and_size(self) -> None:
        model = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", MODEL_SOURCE))
        self.assertEqual(len(model), 65_480)
        self.assertEqual(
            hashlib.sha256(model).hexdigest(),
            "1c8a0d5d995f36659398ec5f1c253d329e878eadd60cc59fec271d3c1f682783",
        )
        self.assertIn("R528_KWS_MODEL_EXPECTED_BYTES 65480", MODEL_HEADER)
        self.assertIn("R528_KWS_CATEGORY_COUNT 7", MODEL_HEADER)
        for expected in (
            "R528_KWS_HUMIDITY_INDEX 2",
            "R528_KWS_LIGHT_INDEX 3",
            "R528_KWS_AIR_QUALITY_INDEX 4",
            "R528_KWS_TEMPERATURE_INDEX 5",
            "R528_KWS_WAKE_INDEX 6",
            "R528_KWS_HUMIDITY_THRESHOLD (50)",
            "R528_KWS_LIGHT_THRESHOLD (-127)",
            "R528_KWS_AIR_QUALITY_THRESHOLD (53)",
            "R528_KWS_TEMPERATURE_THRESHOLD (82)",
            "R528_KWS_WAKE_THRESHOLD (-127)",
        ):
            self.assertIn(expected, MODEL_HEADER)
        self.assertIn(
            "b351168178272b528b07310ea1f2b7b6c3154582cdebf90a1ce8bdf6878cb10b",
            MODEL_SOURCE,
        )

    def test_target_uses_the_exact_host_frontend_source(self) -> None:
        self.assertIn(
            'BRIDGE = ROOT / "contest2026_113_veladuxingke/app/routine_manager/kws_frontend.cc"',
            BUILD_FRONTEND,
        )
        for setting in (
            "kSampleRate = 16000",
            "kWindowSamples = 480",
            "kStrideSamples = 320",
            "kFeatureChannels = 40",
            "lower_band_limit = 125.0f",
            "upper_band_limit = 7500.0f",
            "enable_pcan = 1",
            "scale_shift = 6",
            "kFeatureValueDivisor = 666",
        ):
            self.assertIn(setting, FRONTEND)

    def test_model_shape_quantization_and_ops_are_checked(self) -> None:
        self.assertIn("1, R528_KWS_FEATURE_FRAMES, R528_KWS_FEATURE_CHANNELS, 1", KWS)
        self.assertIn("input->params.scale != 1.0f", KWS)
        self.assertIn("input->params.zero_point != 0", KWS)
        self.assertIn("output->params.scale != (1.0f / 256.0f)", KWS)
        self.assertIn("tflite::MicroMutableOpResolver<5>", KWS)
        for operation in (
            "AddConv2D",
            "AddAveragePool2D",
            "AddReshape",
            "AddFullyConnected",
            "AddSoftmax",
        ):
            self.assertEqual(KWS.count(operation), 1)

    def test_acceptance_requires_argmax_and_validation_threshold(self) -> None:
        decision = KWS[KWS.index("prediction =") : KWS.index("return OK;", KWS.index("prediction ="))]
        for index in (
            "R528_KWS_HUMIDITY_INDEX",
            "R528_KWS_LIGHT_INDEX",
            "R528_KWS_AIR_QUALITY_INDEX",
            "R528_KWS_TEMPERATURE_INDEX",
            "R528_KWS_WAKE_INDEX",
        ):
            self.assertIn("case " + index, decision)
        self.assertIn("prediction < threshold", decision)
        self.assertIn("return -ENODATA", decision)
        for command in (
            "VOICE_COMMAND_QUERY_TEMPERATURE",
            "VOICE_COMMAND_QUERY_HUMIDITY",
            "VOICE_COMMAND_QUERY_LIGHT",
            "VOICE_COMMAND_QUERY_AIR_QUALITY",
            "VOICE_COMMAND_WAKE",
        ):
            self.assertIn("result->command = " + command, decision)
        self.assertIn("result->semantic_verified = true", decision)

    def test_pcm_is_bounded_padded_and_too_short_audio_is_rejected(self) -> None:
        self.assertIn("#define KWS_AUDIO_SAMPLES       57344", KWS)
        self.assertIn("memset(g_kws_audio, 0", KWS)
        self.assertIn("total < KWS_AUDIO_BYTES", KWS)
        self.assertIn("KWS_MIN_AUDIO_SAMPLES", KWS)
        self.assertIn("return -ENODATA", KWS)
        self.assertIn("KWS_TENSOR_ARENA_BYTES  (128 * 1024)", KWS)

    def test_build_enables_tflm_and_registers_frontend_everywhere(self) -> None:
        self.assertIn("config ROUTINE_MANAGER_KWS", KCONFIG)
        self.assertIn("depends on TFLITEMICRO", KCONFIG)
        for config in (
            "CONFIG_MATH_GEMMLOWP=y",
            "CONFIG_MATH_KISSFFT=y",
            "CONFIG_MATH_RUY=y",
            "CONFIG_SYSTEM_FLATBUFFERS=y",
            "CONFIG_TFLITEMICRO=y",
            "CONFIG_ROUTINE_MANAGER_KWS=y",
        ):
            self.assertIn(config, DEFCONFIG)
        for filename in ("voice_recognizer_kws.cc", "kws_frontend.cc", "kws_model_data.cc"):
            self.assertIn(filename, MAKEFILE)
            self.assertIn(filename, CMAKE)
        for filename in ("frontend.c", "fft.cc", "kiss_fft_int16.cc"):
            self.assertIn(filename, TFLM_MAKE)
            self.assertIn(filename, TFLM_CMAKE)

    def test_realtek_sdio_bringup_is_not_repeated_by_board(self) -> None:
        self.assertEqual(CHIP_BOOT.count("realtek_wlan_bringup();"), 1)
        self.assertNotIn("realtek_wlan_bringup();", BOARD_BRINGUP)
        self.assertIn("txfifo_wait_empty", BOARD_BRINGUP)


if __name__ == "__main__":
    unittest.main()
