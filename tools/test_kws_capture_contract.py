#!/usr/bin/env python3
"""Source contracts for the R528 KWS corpus capture phase."""

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "app/routine_manager"
MAIN = (APP / "routine_manager_main.c").read_text(encoding="utf-8")
CAPTURE = (APP / "kws_capture.c").read_text(encoding="utf-8")
KCONFIG = (APP / "Kconfig").read_text(encoding="utf-8")
MAKEFILE = (APP / "Makefile").read_text(encoding="utf-8")
CMAKE = (APP / "CMakeLists.txt").read_text(encoding="utf-8")
AUDIO = (APP / "audio_service.c").read_text(encoding="utf-8")
KWS = (APP / "voice_recognizer_kws.cc").read_text(encoding="utf-8")
SPEC = json.loads((ROOT / "tools/kws_corpus_spec.json").read_text(encoding="utf-8"))


class CaptureContractTests(unittest.TestCase):
    def test_capture_dispatches_before_lvgl_and_reuses_instance_lock(self) -> None:
        dispatch = MAIN.index("if (options.kws_capture)")
        self.assertLess(dispatch, MAIN.index("lv_init();"))
        source = MAIN[dispatch : MAIN.index("if (options.selftest)", dispatch)]
        self.assertIn("g_instance_lock", source)
        self.assertIn("g_instance_running", source)
        self.assertIn("kws_capture_run", source)

    def test_fixed_capture_format_and_persistent_root(self) -> None:
        self.assertIn("#define KWS_CAPTURE_SAMPLE_RATE        16000", CAPTURE)
        self.assertIn("#define KWS_CAPTURE_CHANNELS           1", CAPTURE)
        self.assertIn("#define KWS_CAPTURE_BITS_PER_SAMPLE    16", CAPTURE)
        self.assertIn("AUDIO_FMT_PCM", CAPTURE)
        self.assertIn('default "/sdcard/kws_corpus"', KCONFIG)
        self.assertIn('default "/sdcard"', KCONFIG)
        self.assertIn('CONFIG_ROUTINE_MANAGER_KWS_CAPTURE_ROOT "/sdcard/kws_corpus"', MAIN)
        self.assertIn('CONFIG_ROUTINE_MANAGER_KWS_STORAGE_MOUNT "/sdcard"', MAIN)
        self.assertIn("FATFS_SUPER_MAGIC", CAPTURE)
        self.assertIn("MSDOS_SUPER_MAGIC", CAPTURE)
        self.assertIn("kws_capture_wait_for_storage", CAPTURE)
        self.assertIn("statfs", CAPTURE)

    def test_no_replace_parts_and_cancel_cleanup(self) -> None:
        self.assertIn("O_CREAT | O_EXCL", CAPTURE)
        self.assertIn('"%s/.p%lx.%04u"', CAPTURE)
        self.assertIn('"%s/.j%lx.%04u"', CAPTURE)
        self.assertIn("access(pcm_path, F_OK)", CAPTURE)
        self.assertIn("access(json_path, F_OK)", CAPTURE)
        self.assertIn("rename(pcm_part, pcm_path)", CAPTURE)
        self.assertIn("rename(json_part, json_path)", CAPTURE)
        self.assertIn("rename(pcm_path, pcm_part)", CAPTURE)
        self.assertIn("g_kws_capture_cancelled", CAPTURE)
        self.assertGreaterEqual(CAPTURE.count("unlink(pcm_part)"), 5)

    def test_board_and_host_label_sets_match(self) -> None:
        labels = re.findall(r'^  "([a-z_]+)",?$', CAPTURE, re.M)
        self.assertEqual(labels, list(SPEC["labels"]))
        self.assertEqual(
            {details["training_class"] for details in SPEC["labels"].values()},
            {"target", "unknown", "background", "wake"},
        )
        codes = re.search(
            r"static const char \*const label_codes\[\].*?\{(.*?)\};",
            CAPTURE,
            re.S,
        )
        self.assertIsNotNone(codes)
        board_codes = re.findall(r'"([a-z]{2})"', codes.group(1))
        self.assertEqual(board_codes, list(SPEC["id"]["label_codes"].values()))

    def test_compact_sample_name_fits_nuttx_name_max(self) -> None:
        sample = "k0123456789abcdef_qt_0001"
        self.assertLessEqual(len(sample + ".json"), 32)
        self.assertRegex(sample, SPEC["id"]["sample_pattern"])
        self.assertIn('"k%016llx_%s_%04u"', CAPTURE)
        self.assertIn("CONFIG_NAME_MAX", CAPTURE)

    def test_temporary_names_also_fit_nuttx_name_max(self) -> None:
        for name in (".p7fffffff.0001", ".j7fffffff.0001"):
            self.assertLessEqual(len(name), 32)
        self.assertNotIn(".pcm.part-", CAPTURE)
        self.assertNotIn(".json.part-", CAPTURE)

    def test_build_registration_is_consistent(self) -> None:
        self.assertIn("config ROUTINE_MANAGER_KWS_CAPTURE", KCONFIG)
        self.assertIn("depends on SYSTEM_NXRECORDER", KCONFIG)
        self.assertIn("CONFIG_ROUTINE_MANAGER_KWS_CAPTURE", MAKEFILE)
        self.assertIn("CSRCS  += kws_capture.c", MAKEFILE)
        self.assertIn("CONFIG_ROUTINE_MANAGER_KWS_CAPTURE", CMAKE)
        self.assertIn("list(APPEND SRCS kws_capture.c)", CMAKE)

    def test_capture_plays_temporary_pcm_before_keep_prompt(self) -> None:
        self.assertIn("nxplayer_playraw", CAPTURE)
        self.assertIn("nxplayer_wait", CAPTURE)
        self.assertIn("options->playback_device", CAPTURE)
        playback = CAPTURE.index("kws_capture_playback(options, pcm_part")
        keep = CAPTURE.index("Enter/k 保留", playback)
        publish = CAPTURE.index("kws_capture_publish_pair", keep)
        self.assertLess(playback, keep)
        self.assertLess(keep, publish)

    def test_audio_service_dispatches_to_offline_recognizer(self) -> None:
        self.assertIn("voice_recognizer_recognize", AUDIO)
        self.assertNotIn("voice_recognizer_demo_recognize", AUDIO)
        self.assertNotIn("mimo", AUDIO.lower())
        self.assertNotIn("tflite", AUDIO.lower())
        self.assertNotIn("kws_capture", AUDIO)
        self.assertIn("tflite::MicroInterpreter", KWS)
        self.assertIn("result->semantic_verified = true", KWS)

    def test_kws_options_are_rejected_without_mode(self) -> None:
        self.assertIn("KWS options require --kws-capture", MAIN)
        self.assertIn("kws_capture_validate_options", MAIN)
        self.assertIn("kws_capture_selftest", MAIN)

    def test_short_form_fits_nsh_line_limit(self) -> None:
        command = "routine_mgr -K -l query_temperature -p s001 -s smoke_a -n 1"
        self.assertLess(len(command), 64)
        for option in ('"-K"', '"-l"', '"-p"', '"-s"', '"-n"', '"-m"'):
            self.assertIn(option, MAIN)
        self.assertIn("NSH has a 64-byte line limit", MAIN)

    def test_preflight_failures_are_visible(self) -> None:
        self.assertIn("kws capture: starting label=", CAPTURE)
        self.assertIn("kws capture: cannot prepare", CAPTURE)
        self.assertIn("kws capture: statfs", CAPTURE)
        self.assertIn("kws capture: insufficient storage", CAPTURE)
        self.assertIn("routine_mgr: KWS capture failed", MAIN)

    def test_kws_output_is_restricted_to_sd_mount(self) -> None:
        self.assertIn("storage_mountpoint", CAPTURE)
        self.assertIn("kws_capture_path_under_mount", CAPTURE)
        self.assertIn("never falls back to internal /data storage", KCONFIG)

    def test_micro_tf_is_enabled_for_board(self) -> None:
        board = (ROOT / "board/r528s3-dshanpi/Kconfig").read_text()
        config = (ROOT.parent / "nuttx/.config").read_text()
        self.assertIn("select MICRO_TF", board)
        self.assertIn("CONFIG_MICRO_TF=y", config)
        defconfig = (ROOT / "board/r528s3-dshanpi/configs/nsh/defconfig").read_text()
        self.assertIn("CONFIG_DRIVERS_SDMMC=y", defconfig)
        self.assertIn("CONFIG_FS_FATFS=y", defconfig)


if __name__ == "__main__":
    unittest.main()
