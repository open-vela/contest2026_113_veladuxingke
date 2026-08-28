#!/usr/bin/env python3
"""Tests for deterministic MiMo routine voice mastering v3."""

import hashlib
import importlib.util
import json
import math
import struct
import tempfile
import unittest
import wave
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("prepare_voice_assets.py")
SPEC = importlib.util.spec_from_file_location("prepare_voice_assets", MODULE_PATH)
voice_assets = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(voice_assets)

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = PROJECT_ROOT / "source_assets" / "routine_voice"
MODEL = "mimo-v2.5-tts"
SPEAKER = "冰糖"
SOURCE_URL = (
    "https://mimo.mi.com/docs/zh-CN/quick-start/usage-guide/"
    "audio/speech-synthesis-v2.5"
)
AUTHORIZATION_NOTE = (
    "User-provided contest authorization; mastering v3 sensor speech "
    "candidate pending hardware validation"
)
CUE_SHA256 = "43fd0adab4d368781dde6325aea02d4a3d1c81f5b5206971c7223a9795ba43ea"
SILENCE_SHA256 = "a11937f356a9b0ba592c82f5290bac8016cb33a3f9bc68d3490147c158ebb10d"
SOURCE_SHA256 = {
    "wake_ack": "aaac7d94e8555f886036dc50e782f1cad41ca6aae2b9974c9c310be839efd1c8",
    "temperature_low_warning": "b1d354fa52d80cab6ad549c4183250d999ebe55c463411f66705fd64ff9e05ee",
    "temperature_high_warning": "ffcfbd7627538d9d406ea3f2e6089b8c61e1edfe5cc5a048d788c97017938e64",
    "current_time_is": "9120b658c964da30a08621386ba2104f80ef1e3b793fd1ca255d2daae01f5747",
    "second_unit": "38ad4066c52d25ae9ee86f3cbaabc0eb601cdae4cfd857152e697c1604319a8e",
    "today_goal_due": "10a856c2fc8ed503238f54078b9fdbd493ab166088ee968d41830e9968dc0a24",
    "current_temperature_is": "573397b4e6560c6beab9ccce9d69e0c6b6a5b0bb40646fff0fa71f305fad1524",
    "below_zero": "f4dde8dc72f786af6980ebd3ca75fc81de7018270935313d063a808ce92a8f16",
    "zero": "c188247976551036696751fb84ae8ce999eae9e424c5c3e94a399d66e12a567e",
    "one": "c72691ba4836a937eb77fcbb9ba647cc755c166162d74d09f040339b1d755c00",
    "two": "21a744802fce4f5c0cac0f76fc5608e3193115dd2280d75747043ed1036d1ae7",
    "three": "4dad682770d580fbbd9f3acace66e44c9bb621af35a2a2afdaf4e2c22e085e2a",
    "four": "cc251b1e98622d09f8749f2755b03555aaed19988652eaa19adc5efdcc1c9700",
    "five": "d6422a0f945c7783c49c038e6b99e6c6747adc879532ee91ca93e2cd738a9556",
    "six": "0fd700aa485938d9c9d0345a14dbb89e4d1ce30737fd9d675392f9dece9f0fd9",
    "seven": "e47d0ab6c1d6460869ada82305ba7449f20f1ff3657223aec14ee01b1189f047",
    "eight": "c679cf55631a4528db51a2b6742d373bc324bc17e1a6838e616f3ae1afeae80c",
    "nine": "e12c983a77e015ab40c9d2dc6a7ecb6c0f7cdf1222720a613ed90c5f6bfdc5db",
    "ten": "c3376cec31ecf216101878a97036ca8c8e0c562abc0e9f6c1f446c526b049326",
    "hundred": "de4a69122bc4b242fa25f60c922f0a5f19cfa112bfa65c688b5c6a7b2d63a6e4",
    "thousand": "b99cae88eb5a95fb9e928b4df05e2191741fd546e02c49b3172d72e6ac94c34d",
    "ten_thousand": "f37d3cadba89aa1fcfb43233d8c9dad71092c8daead320649b0d61633fda8b05",
    "point": "93fe1da3de5150191d02986db871bd080fd4a4f3b2a18e6ed5a20ff93445f2a2",
    "degree_celsius": "a237d0c8a23706d94106da6bc59cd4f3f81d8d8afa2ca1fdadbeea11772f5763",
    "current_humidity_is": "b3b1b1a20a0a56f6a0797d555bedb9509b2f9ef5d33d7b49f4bd7a1959d90549",
    "percentage": "b9f3a87c57832e013976f844f0670d0c873155c40ed0d7b40454fd9c85f73159",
    "current_light_is": "3db94fb7c738aaeb1bb850a7f3e46406ca606c78a05a71c391e29d84bc57c82a",
    "lux": "b675c78a3dbd86e7ced006771162991b23bf9c4eec1db287d67e493e7f53c360",
    "carbon_dioxide_concentration_is": "a8a85664e0c01cb618956c848333220c4670c28557105d0913d05ffb903ce9eb",
    "ppm": "80b9b1d59cb317e34f490e1b3002763fdbb885716de03ba7868e7eb76c5cd2b7",
    "volatile_organic_compound_concentration_is": "d47cbfa430e21e61f2124b622fb758c5b70c2eafbb0aa01d1646e54b2c6f51be",
    "ppb": "9599e22b1c42201650f085f9d2a033e07f80c47335f36da47bc06f2307d54a37",
    "schedule_reminder": "ae64b0f44c1fb031841c22bc86d2a7eaee539df1dd170f4edc91d8d5b47faf07",
    "schedule_drink": "0b1a8f7d6661cc70605c393188927f75524d1fb0b9564219b7d6b4d6b2742138",
    "schedule_focus": "7e1233237fd72113538ab1c5f88b61ccb68a18c31ba1dd092205e92be01d56fb",
    "schedule_ventilate": "8384f6826b1857167a80eb47ebf989f4ab2a7fdd1682aff3dd61a340ce27912d",
    "hour_unit": "e0be980970de303d05fd775dbc5fd3453762068b14fc8ea162b124c65eb6da1c",
    "minute_unit": "4f67399a9a87a4926d46e89e1afc25848b6580ffa1087de6735ee8224b107be1",
    "whole_hour": "807c82040f8153efb4bd9761378e1a91e85377e4426fa3d45cd40f502f377406",
    "half_hour": "63a0f0e4270b772ceddaae6396ab62632ae91ee9ec784d3971a4be3fd3c7d92b",
}
RESAMPLE_GOLDEN_SHA256 = "2e8136eb655167c308e7bf9de6c9ae5d0f29369914ac0d02c916a839575f7506"


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_mimo_wav(path: Path, samples: list[int], rate: int = 24000,
                    channels: int = 1, width: int = 2) -> None:
    with wave.open(str(path), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(width)
        output.setframerate(rate)
        if width != 2:
            raise ValueError("test helper only packs PCM16")
        if channels == 1:
            output.writeframes(struct.pack(f"<{len(samples)}h", *samples))
        else:
            interleaved = [sample for value in samples for sample in (value,) * channels]
            output.writeframes(struct.pack(f"<{len(interleaved)}h", *interleaved))


def sine_samples(rate: int, duration_ms: int, amplitude: int,
                 frequency: float = 440.0) -> list[int]:
    count = rate * duration_ms // 1000
    return [
        voice_assets.round_ties_away(
            amplitude * math.sin(2.0 * math.pi * frequency * index / rate)
        )
        for index in range(count)
    ]


class PrimitiveTests(unittest.TestCase):
    def test_rounding_ties_away_from_zero(self) -> None:
        self.assertEqual(
            [voice_assets.round_ties_away(value) for value in
             (1.5, 1.49, -1.5, -1.49)],
            [2, 1, -2, -1],
        )

    def test_resample_golden_vector_and_hash(self) -> None:
        source = [0, 12000, -12000, 8000, -8000, 3276, -3276, 1, -1]
        output = voice_assets.resample_linear(source, 24000, 16000)
        packed = struct.pack(f"<{len(output)}h", *output)
        self.assertEqual(output, [0, 0, 8000, -2362, -3276, 0])
        self.assertEqual(hashlib.sha256(packed).hexdigest(), RESAMPLE_GOLDEN_SHA256)

    def test_active_region_ignores_extra_edge_silence(self) -> None:
        speech = sine_samples(16000, 200, 5000)
        first = [0] * 1600 + speech + [0] * 1600
        second = [0] * 4800 + speech + [0] * 4000
        first_region = voice_assets.detect_active_region(first)
        second_region = voice_assets.detect_active_region(second)
        first_slice = first[
            first_region["measurement_start_frame"]:
            first_region["measurement_end_frame"]
        ]
        second_slice = second[
            second_region["measurement_start_frame"]:
            second_region["measurement_end_frame"]
        ]
        self.assertEqual(first_slice, second_slice)
        self.assertEqual(first_region["measurement_frames"],
                         second_region["measurement_frames"])

    def test_boundary_fades(self) -> None:
        samples = [1000] * 400
        faded = voice_assets.apply_boundary_fades(samples)
        fade = voice_assets.TARGET_RATE * voice_assets.FADE_MS // 1000
        self.assertEqual(faded[0], 0.0)
        self.assertEqual(faded[-1], 0.0)
        self.assertAlmostEqual(faded[fade], 1000.0)
        self.assertAlmostEqual(faded[-fade - 1], 1000.0)
        self.assertLess(faded[1], faded[fade - 1])
        self.assertLess(faded[-2], faded[-fade])

    def test_compressor_steady_state_and_attack_release(self) -> None:
        high = [24000.0] * 8000
        low = [1000.0] * 8000
        output, metadata = voice_assets.compress_samples(high + low)
        expected = voice_assets.compressor_gain(24000.0)
        steady_gain = output[7999] / high[-1]
        attack_gain = output[0] / high[0]
        release_start = output[8000] / low[0]
        release_end = output[-1] / low[-1]
        self.assertAlmostEqual(steady_gain, expected, places=3)
        self.assertGreater(attack_gain, steady_gain)
        self.assertLess(release_start, release_end)
        self.assertAlmostEqual(release_end, 1.0, places=2)
        self.assertGreater(metadata["maximum_gain_reduction_db"], 0.0)

    def test_limiter_impulse_never_exceeds_ceiling(self) -> None:
        samples = [0.0] * 400
        samples[200] = 100000.0
        output, metadata = voice_assets.limit_samples(samples)
        quantized = voice_assets.quantize_int16(output)
        self.assertLessEqual(max(map(abs, quantized)), voice_assets.PEAK_CEILING)
        self.assertGreater(metadata["maximum_gain_reduction_db"], 0.0)
        lookahead = voice_assets.TARGET_RATE * voice_assets.LIMITER_LOOKAHEAD_MS // 1000
        self.assertLess(abs(output[200 - lookahead]), 1.0)

    def test_invalid_format_silence_and_clipping_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            wrong_rate = root / "wrong-rate.wav"
            silent = root / "silent.wav"
            clipped = root / "clipped.wav"
            write_mimo_wav(wrong_rate, [100, -100] * 100, rate=16000)
            write_mimo_wav(silent, [0] * 2400)
            write_mimo_wav(clipped, [32767, -32767] * 1200)
            for path in (wrong_rate, silent, clipped):
                with self.subTest(path=path.name):
                    with self.assertRaises(ValueError):
                        voice_assets.convert_wav(path)

    def test_unreachable_target_and_heavy_limiting_fail(self) -> None:
        samples = [0] * 1600 + sine_samples(16000, 200, 1000) + [0] * 1600
        active = voice_assets.detect_active_region(samples)
        original_ceiling = voice_assets.PEAK_CEILING
        original_limit = voice_assets.MAX_LIMITER_REDUCTION_DB
        try:
            voice_assets.PEAK_CEILING = 1
            with self.assertRaisesRegex(ValueError, "unreachable"):
                voice_assets.master_mono(samples, active)
            voice_assets.PEAK_CEILING = original_ceiling
            voice_assets.MAX_LIMITER_REDUCTION_DB = -0.1
            with self.assertRaisesRegex(ValueError, "too heavily"):
                voice_assets.master_mono(samples, active)
        finally:
            voice_assets.PEAK_CEILING = original_ceiling
            voice_assets.MAX_LIMITER_REDUCTION_DB = original_limit


class ProductionAssetTests(unittest.TestCase):
    def generate(self, output: Path) -> dict:
        return voice_assets.prepare_assets(
            SOURCE_DIR,
            output,
            MODEL,
            SPEAKER,
            SOURCE_URL,
            AUTHORIZATION_NOTE,
        )

    def test_master_set_and_source_hashes_are_locked(self) -> None:
        wavs = {path.stem for path in SOURCE_DIR.glob("*.wav")}
        self.assertEqual(wavs, set(voice_assets.REQUIRED))
        self.assertEqual(set(SOURCE_SHA256), set(voice_assets.REQUIRED))
        for stem, expected in SOURCE_SHA256.items():
            self.assertEqual(file_sha256(SOURCE_DIR / f"{stem}.wav"), expected)

        generation = json.loads(
            (SOURCE_DIR / "mimo-generation.json").read_text(encoding="utf-8")
        )
        self.assertEqual(set(generation["prompts"]), set(voice_assets.REQUIRED))
        for stem, expected in SOURCE_SHA256.items():
            source = SOURCE_DIR / f"{stem}.wav"
            prompt = generation["prompts"][stem]
            self.assertEqual(prompt["wav_sha256"], expected)
            self.assertEqual(prompt["wav_bytes"], source.stat().st_size)
            with wave.open(str(source), "rb") as wav:
                self.assertEqual(prompt["wav"]["channels"], wav.getnchannels())
                self.assertEqual(prompt["wav"]["sample_width"], wav.getsampwidth())
                self.assertEqual(prompt["wav"]["sample_rate"], wav.getframerate())
                self.assertEqual(prompt["wav"]["frames"], wav.getnframes())

    def test_generated_assets_are_valid_complete_and_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as first_temp, \
             tempfile.TemporaryDirectory() as second_temp:
            first = Path(first_temp)
            second = Path(second_temp)
            manifest = self.generate(first)
            self.generate(second)

            first_files = sorted(path.name for path in first.iterdir())
            second_files = sorted(path.name for path in second.iterdir())
            self.assertEqual(first_files, second_files)
            for name in first_files:
                self.assertEqual((first / name).read_bytes(),
                                 (second / name).read_bytes())

            pcm_names = {path.name for path in first.glob("*.pcm")}
            expected = {
                voice_assets.RUNTIME_FILES.get(stem, f"{stem}.pcm")
                for stem in voice_assets.REQUIRED
            }
            expected.update({"cue.pcm", "silence_80ms.pcm"})
            self.assertEqual(pcm_names, expected)
            self.assertEqual(file_sha256(first / "cue.pcm"), CUE_SHA256)
            self.assertEqual(file_sha256(first / "silence_80ms.pcm"),
                             SILENCE_SHA256)

            normalization = manifest["normalization"]
            self.assertEqual(normalization["version"], 3)
            self.assertEqual(
                normalization["vad"]["trimming"],
                "remove excess leading/trailing silence; retain 20 ms pre-roll and 40 ms post-roll",
            )
            self.assertEqual(normalization["limiter"]["ceiling"],
                             voice_assets.PEAK_CEILING)
            self.assertEqual(normalization["rounding"],
                             "nearest integer, ties away from zero")
            self.assertEqual(
                manifest["generator"]["generation_manifest_sha256"],
                file_sha256(SOURCE_DIR / "mimo-generation.json"),
            )

            for stem in voice_assets.REQUIRED:
                metadata = manifest["assets"][stem]
                path = first / metadata["runtime_file"]
                data = path.read_bytes()
                self.assertEqual(len(data) % 4, 0)
                self.assertEqual(len(data), metadata["frames"] * 4)
                self.assertEqual(len(data), metadata["bytes"])
                self.assertEqual(file_sha256(path), metadata["output_sha256"])
                self.assertEqual(metadata["source_sha256"], SOURCE_SHA256[stem])
                self.assertEqual(metadata["source"], f"{stem}.wav")
                self.assertEqual(metadata["source_rate"], 24000)
                self.assertEqual(metadata["source_channels"], 1)
                self.assertEqual(metadata["source_width"], 2)

                mono = []
                full_scale = 0
                for left, right in struct.iter_unpack("<hh", data):
                    self.assertEqual(left, right)
                    mono.append(left)
                    full_scale += left in (-32768, 32767)
                peak = max(map(abs, mono))
                mastering = metadata["mastering"]
                active = metadata["active_region"]
                trim = metadata["trim"]
                self.assertTrue(trim["enabled"])
                self.assertEqual(trim["retained_preroll_ms"], 20)
                self.assertEqual(trim["retained_postroll_ms"], 40)
                self.assertLessEqual(metadata["frames"], metadata["source_frames"])
                self.assertEqual(
                    metadata["frames"], trim["end_frame"] - trim["start_frame"]
                )
                active_values = mono[
                    active["measurement_start_frame"]:
                    active["measurement_end_frame"]
                ]
                active_dbfs = voice_assets.dbfs(voice_assets.rms(active_values))
                self.assertLessEqual(peak, voice_assets.PEAK_CEILING)
                self.assertEqual(full_scale, 0)
                self.assertEqual(mastering["full_scale_samples"], 0)
                self.assertEqual(peak, mastering["post_peak"])
                self.assertLessEqual(
                    abs(active_dbfs - voice_assets.TARGET_ACTIVE_RMS_DBFS),
                    voice_assets.TARGET_ACTIVE_RMS_TOLERANCE_DB,
                )
                self.assertEqual(active_dbfs, mastering["active_rms_dbfs"])
                self.assertLessEqual(
                    mastering["limiter"]["maximum_gain_reduction_db"],
                    voice_assets.MAX_LIMITER_REDUCTION_DB,
                )
                self.assertEqual(mono[0], 0)
                self.assertEqual(mono[-1], 0)

                # A clipping plateau would contain adjacent ceiling-valued samples.
                ceiling_run = 0
                maximum_ceiling_run = 0
                for value in mono:
                    if abs(value) >= voice_assets.PEAK_CEILING:
                        ceiling_run += 1
                        maximum_ceiling_run = max(maximum_ceiling_run, ceiling_run)
                    else:
                        ceiling_run = 0
                self.assertLessEqual(maximum_ceiling_run, 1)

                required_active_keys = {
                    "frame_ms", "threshold_dbfs",
                    "measurement_start_frame", "measurement_end_frame",
                    "measurement_frames", "preroll_ms", "postroll_ms",
                }
                self.assertTrue(required_active_keys.issubset(active))
                self.assertTrue({
                    "fade_ms", "compressor", "makeup_gain_db", "limiter",
                    "target_active_rms_dbfs", "active_rms", "active_rms_dbfs",
                    "post_peak", "post_peak_dbfs", "full_scale_samples",
                }.issubset(mastering))

            loaded = json.loads(
                (first / "assets.json").read_text(encoding="utf-8")
            )
            self.assertEqual(loaded, manifest)


if __name__ == "__main__":
    unittest.main()
