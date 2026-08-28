#!/usr/bin/env python3
"""Static contracts for serialized routine audio playback lifecycles."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
AUDIO = (ROOT / "app/routine_manager/audio_service.c").read_text(encoding="utf-8")
UI = (ROOT / "app/routine_manager/routine_ui.c").read_text(encoding="utf-8")
MAIN = (ROOT / "app/routine_manager/routine_manager_main.c").read_text(encoding="utf-8")
NXPLAYER = (WORKSPACE / "apps/system/nxplayer/nxplayer.c").read_text(encoding="utf-8")
R528 = (WORKSPACE / "vendor/allwinnertech/chips/r528/components/audio/sunxi_alsa.c").read_text(encoding="utf-8")


class AudioLifecycleContractTests(unittest.TestCase):
    def test_worker_prioritizes_due_reminders_atomically(self) -> None:
        claim = re.search(r"static void audio_service_claim_locked.*?\n}\n", AUDIO, re.S)
        self.assertIsNotNone(claim)
        source = claim.group(0)
        self.assertIn("reminder_count > 0", source)
        self.assertLess(source.index("reminder_count > 0"),
                        source.index("pending_operation"))
        self.assertIn("operation->values = service->sensor.values", source)
        self.assertIn("service->worker_active = true", source)

    def test_process_uses_immutable_operation_snapshot(self) -> None:
        process = re.search(r"static void audio_service_process\(.*?\n}\n\nstatic void audio_service_claim_locked", AUDIO, re.S)
        self.assertIsNotNone(process)
        source = process.group(0)
        self.assertIn("request = operation->request", source)
        self.assertIn("values = operation->values", source)
        self.assertNotIn("request = service->request", source)
        self.assertNotIn("values = service->sensor.values", source)

    def test_direct_requests_cannot_overtake_reminders(self) -> None:
        self.assertGreaterEqual(AUDIO.count("service->reminder_count > 0"), 3)
        self.assertIn("audio_service_claim_locked(service);", AUDIO)
        self.assertIn("retry reminder=", AUDIO)

    def test_button_query_is_one_capture_without_wake_gate(self) -> None:
        process = re.search(
            r"static void audio_service_process\(.*?\n}\n\nstatic void audio_service_claim_locked",
            AUDIO,
            re.S,
        )
        self.assertIsNotNone(process)
        source = process.group(0)
        self.assertEqual(source.count("audio_service_capture(service, generation)"), 1)
        self.assertEqual(source.count("voice_recognizer_recognize("), 1)
        self.assertIn("audio_service_query_request_spec(", source)
        self.assertNotIn("VOICE_COMMAND_WAKE", source)
        self.assertNotIn("VOICE_REPLY_WAKE_ACK", source)

    def test_query_maps_all_four_sensor_commands(self) -> None:
        query = re.search(
            r"static int audio_service_query_request_spec\(.*?\n}\n",
            AUDIO,
            re.S,
        )
        self.assertIsNotNone(query)
        source = query.group(0)
        for command, request in (
            ("VOICE_COMMAND_QUERY_TEMPERATURE", "AUDIO_SERVICE_REQUEST_TEMPERATURE"),
            ("VOICE_COMMAND_QUERY_HUMIDITY", "AUDIO_SERVICE_REQUEST_HUMIDITY"),
            ("VOICE_COMMAND_QUERY_LIGHT", "AUDIO_SERVICE_REQUEST_BH1750"),
            ("VOICE_COMMAND_QUERY_AIR_QUALITY", "AUDIO_SERVICE_REQUEST_SGP30"),
        ):
            self.assertIn(command, source)
            self.assertIn(request, source)

    def test_voice_timer_only_observes_and_never_auto_restarts(self) -> None:
        timer = re.search(r"static void routine_voice_timer_cb.*?\n}\n", MAIN, re.S)
        self.assertIsNotNone(timer)
        self.assertIn("audio_service_get_snapshot", timer.group(0))
        self.assertNotIn("audio_service_begin", timer.group(0))
        action = re.search(r"static int routine_voice_action_cb.*?\n}\n", MAIN, re.S)
        self.assertIsNotNone(action)
        self.assertIn("audio_service_begin", action.group(0))

    def test_nxplayer_stop_sends_outside_mutex_and_waits(self) -> None:
        stop = re.search(r"int nxplayer_stop\(.*?\n}\n#endif", NXPLAYER, re.S)
        self.assertIsNotNone(stop)
        source = stop.group(0)
        unlock = source.index("pthread_mutex_unlock")
        send = source.index("mq_timedsend")
        self.assertLess(unlock, send)
        self.assertIn("nxplayer_wait(pplayer)", source)
        self.assertIn("nxplayer_refput(pplayer)", NXPLAYER)
        playthread = re.search(r"static FAR void \*nxplayer_playthread.*?return NULL;\n}", NXPLAYER, re.S)
        self.assertIsNotNone(playthread)
        self.assertNotIn("nxplayer_release(pplayer)", playthread.group(0))

    def test_r528_worker_does_not_close_control_queue(self) -> None:
        start = R528.index("static void play_workerthread(void *arg)",
                           R528.index("static void play_workerthread(void *arg)") + 1)
        end = R528.index("static void sunxi_audio_reset", start)
        worker = R528[start:end]
        self.assertNotIn("file_mq_close(&priv->mq)", worker)
        stop_start = R528.index("static int sunxi_audio_stop(FAR struct audio_lowerhalf_s *dev)",
                                R528.index("static int sunxi_audio_stop(FAR struct audio_lowerhalf_s *dev)") + 1)
        stop_end = R528.index("static int sunxi_audio_pause", stop_start)
        source = R528[stop_start:stop_end]
        self.assertIn("!priv->natural_complete", source)
        self.assertIn("snd_vela_pcm_drop", source)
        self.assertIn("snd_vela_pcm_drain", source)
        self.assertIn("priv->mq_opened", source)

    def test_ui_reports_busy_instead_of_silent_acceptance(self) -> None:
        self.assertIn("ret == -EBUSY", UI)
        self.assertIn("g_ui.voice_active ? g_ui.text->voice_busy", UI)
        self.assertIn("snapshot->pending_reminders > 0", UI)

    def test_ui_tracks_the_recognized_sensor_type(self) -> None:
        self.assertIn("snapshot->result_request", UI)
        for request in (
            "AUDIO_SERVICE_REQUEST_TEMPERATURE",
            "AUDIO_SERVICE_REQUEST_HUMIDITY",
            "AUDIO_SERVICE_REQUEST_BH1750",
            "AUDIO_SERVICE_REQUEST_SGP30",
        ):
            self.assertIn(request, UI)


if __name__ == "__main__":
    unittest.main()
