#!/usr/bin/env python3
"""Static contracts for boot startup and Wi-Fi-triggered clock sync."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RCS = (ROOT / "board/r528s3-dshanpi/src/etc/init.d/rcS").read_text()
WIFI = (ROOT / "app/wifi_manager/wifi_manager_main.c").read_text()
TIME_SYNC = (ROOT / "app/wifi_manager/wifi_time_sync.c").read_text()
ROUTINE = (ROOT / "app/routine_manager/routine_manager_main.c").read_text()


class BootTimeContractTests(unittest.TestCase):
    def test_contest_services_start_once_from_romfs_boot_script(self) -> None:
        for command in ("wifi_manager", "lan_panel", "routine_mgr"):
            self.assertEqual(
                len(re.findall(rf"^{command}\s*&\s*$", RCS, re.M)), 1, command
            )
        self.assertNotIn("/data/xiaozhi.sh", RCS)
        self.assertNotIn("xiaozhi_gui", RCS)
        self.assertNotIn("control_center", RCS)

    def test_ntp_runs_only_after_wifi_has_ipv4_and_retries(self) -> None:
        ready = WIFI.split("if (associated && wifi_manager_ipv4_ready())", 1)[1]
        self.assertIn("wifi_manager_sync_time();", ready)
        self.assertIn("WIFI_MANAGER_TIME_RETRY_SEC", WIFI)
        self.assertIn("WIFI_MANAGER_TIME_RESYNC_SEC", WIFI)
        self.assertIn(r'\"time_synchronized\"', WIFI)

    def test_ntp_request_does_not_require_icmp_ping(self) -> None:
        self.assertIn("SOCK_DGRAM", TIME_SYNC)
        self.assertIn("IPPROTO_UDP", TIME_SYNC)
        self.assertIn("clock_settime(CLOCK_REALTIME", TIME_SYNC)
        self.assertIn("WIFI_NTP_UNIX_DELTA", TIME_SYNC)
        self.assertNotIn("netlib_check_ipconnectivity", TIME_SYNC)
        self.assertNotIn("ntpc_start", WIFI)

    def test_routine_clock_keeps_following_realtime_without_wifi(self) -> None:
        timer = re.search(r"static void routine_timer_cb.*?\n}\n", ROUTINE, re.S)
        self.assertIsNotNone(timer)
        source = timer.group(0)
        self.assertIn("routine_get_local_clock(&clock)", source)
        self.assertNotIn("wifi", source.lower())


if __name__ == "__main__":
    unittest.main()
