#!/usr/bin/env python3
"""Static contracts for the lan_panel HTTP server and its served page."""

import re
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
MAIN_PATH = PROJECT_ROOT / "app" / "lan_panel" / "lan_panel_main.c"
HTTP_PATH = PROJECT_ROOT / "app" / "lan_panel" / "lan_http.c"
HEADER_PATH = PROJECT_ROOT / "app" / "lan_panel" / "lan_http.h"
AUTH_PATH = PROJECT_ROOT / "app" / "lan_panel" / "lan_auth.c"
GPIO_DRIVER_PATH = (PROJECT_ROOT.parent / "vendor" / "allwinnertech" / "chips" /
                    "r528" / "drv" / "gpio" / "drv_gpio.c")
BOARD_MAKEFILE_PATH = (PROJECT_ROOT / "board" / "r528s3-dshanpi" / "src" /
                       "Makefile")
ROUTINE_STORAGE_PATH = (PROJECT_ROOT / "app" / "routine_manager" /
                        "routine_storage.c")
MAIN_SOURCE = MAIN_PATH.read_text(encoding="utf-8")
HTTP_SOURCE = HTTP_PATH.read_text(encoding="utf-8")
HEADER_SOURCE = HEADER_PATH.read_text(encoding="utf-8")
AUTH_SOURCE = AUTH_PATH.read_text(encoding="utf-8")
GPIO_DRIVER_SOURCE = GPIO_DRIVER_PATH.read_text(encoding="utf-8")
BOARD_MAKEFILE_SOURCE = BOARD_MAKEFILE_PATH.read_text(encoding="utf-8")
ROUTINE_STORAGE_SOURCE = ROUTINE_STORAGE_PATH.read_text(encoding="utf-8")


class StandaloneTokenTests(unittest.TestCase):
    def test_token_is_exactly_four_decimal_digits(self) -> None:
        self.assertIn("#define LAN_AUTH_TEXT 4", AUTH_SOURCE)
        self.assertIn('snprintf(generated, sizeof(generated), "%04u"',
                      AUTH_SOURCE)
        self.assertIn("value % 10000", AUTH_SOURCE)
        self.assertIn("#define LAN_PANEL_TOKEN_MAX 5", MAIN_SOURCE)
        self.assertIn("/^\\\\d{4}$/.test(t)", MAIN_SOURCE)

    def test_legacy_long_token_is_detected_and_replaced(self) -> None:
        loader = AUTH_SOURCE.split("static int lan_auth_load_file", 1)[1]
        loader = loader[:loader.index("int lan_auth_load(")]
        self.assertIn("char stored[LAN_AUTH_TEXT + 1]", loader)
        self.assertIn("read(fd, stored, sizeof(stored))", loader)
        self.assertIn("length != LAN_AUTH_TEXT", loader)

    def test_token_is_not_disclosed_on_the_serial_console(self) -> None:
        self.assertIn("four-digit access token is ready", MAIN_SOURCE)
        self.assertNotIn("bootstrap token=", MAIN_SOURCE)
        self.assertNotRegex(MAIN_SOURCE, r'printf\([^;]*token[^;]*%s')


class ListenBacklogTests(unittest.TestCase):
    def test_backlog_accepts_parallel_browser_connections(self) -> None:
        match = re.search(r"^#define\s+LAN_PANEL_BACKLOG\s+(\d+)\s*$",
                          MAIN_SOURCE, re.M)
        self.assertIsNotNone(match, "missing LAN_PANEL_BACKLOG")
        self.assertGreaterEqual(int(match.group(1)), 4)

    def test_listen_uses_the_backlog_constant(self) -> None:
        self.assertIn("listen(listensd, LAN_PANEL_BACKLOG)", MAIN_SOURCE)
        self.assertNotIn("listen(listensd, 1)", MAIN_SOURCE)

    def test_connections_are_served_by_bounded_detached_workers(self) -> None:
        self.assertIn("#define LAN_PANEL_MAX_WORKERS 4", MAIN_SOURCE)
        self.assertIn("#define LAN_PANEL_WORKER_STACKSIZE", MAIN_SOURCE)
        self.assertIn("PTHREAD_CREATE_DETACHED", MAIN_SOURCE)
        self.assertIn("lan_panel_connection_worker", MAIN_SOURCE)
        self.assertIn("pthread_create(&worker_thread", MAIN_SOURCE)
        self.assertIn("lan_panel_wait_for_workers()", MAIN_SOURCE)
        worker = MAIN_SOURCE.split(
            "static void *lan_panel_connection_worker", 1
        )[1]
        worker = worker[:worker.index("Light Control Thread")]
        self.assertIn("lan_panel_handle(fd, NULL)", worker)
        self.assertIn("lan_http_close(fd)", worker)

    def test_mutating_endpoints_are_serialized(self) -> None:
        self.assertIn("g_mutation_lock", MAIN_SOURCE)
        handler = MAIN_SOURCE.split("static int lan_panel_handle", 1)[1]
        self.assertGreaterEqual(
            handler.count("pthread_mutex_lock(&g_mutation_lock)"), 4
        )
        self.assertEqual(
            handler.count("pthread_mutex_lock(&g_mutation_lock)"),
            handler.count("pthread_mutex_unlock(&g_mutation_lock)"),
        )


class IdleConnectionTests(unittest.TestCase):
    def test_idle_preconnect_is_dropped_without_a_reply(self) -> None:
        self.assertIn("return used == 0 ? -ENODATA : -ECONNRESET;",
                      HTTP_SOURCE)
        handler = MAIN_SOURCE.split("static int lan_panel_handle", 1)[1]
        self.assertIn("if (ret == -ENODATA)", handler)
        enodata = handler.split("if (ret == -ENODATA)", 1)[1]
        bad_request = enodata.index("Bad request")
        self.assertLess(enodata.index("return 0;"), bad_request)

    def test_connection_close_drains_before_shutdown(self) -> None:
        self.assertIn("void lan_http_close(int fd)", HTTP_SOURCE)
        self.assertIn("void lan_http_close(int fd);", HEADER_SOURCE)
        self.assertIn("lan_http_close(acceptsd);", MAIN_SOURCE)
        self.assertIsNone(re.search(r"(?<!lan_http_)close\(acceptsd\)",
                                    MAIN_SOURCE))
        body = HTTP_SOURCE.split("void lan_http_close(int fd)", 1)[1]
        self.assertLess(body.index("shutdown(fd, SHUT_WR)"),
                        body.index("recv(fd, discard"))


class RequestBodyTests(unittest.TestCase):
    def test_header_lookup_does_not_destroy_later_headers(self) -> None:
        reader = HTTP_SOURCE.split("static int lan_http_header_value", 1)[1]
        reader = reader[:reader.index("int lan_http_read_request")]
        self.assertIn("const char *headers", reader)
        self.assertIn("lan_http_find_crlf", reader)
        self.assertIn("memcpy(value, value_start, length)", reader)
        self.assertNotIn("*end = '\\0'", reader)

    def test_authorization_and_content_length_are_parsed_independently(
            self) -> None:
        reader = HTTP_SOURCE.split("int lan_http_read_request", 1)[1]
        reader = reader[:reader.index("static const char *lan_http_reason")]
        self.assertIn('"Authorization"', reader)
        self.assertIn('"Content-Length"', reader)
        self.assertIn("request->body_length = already", reader)
        self.assertIn("content_length - request->body_length", reader)


class StatusEndpointTests(unittest.TestCase):
    def test_status_read_retries_across_publish_rename(self) -> None:
        handler = MAIN_SOURCE.split('"/api/v1/status"', 1)[1]
        block = handler[:handler.index("Status unavailable")]
        self.assertIn("for (attempt = 0; attempt < 3; attempt++)", block)
        self.assertIn("lan_panel_read_all(LAN_PANEL_STATUS", block)
        self.assertIn("usleep(", block)

    def test_status_reader_consumes_the_whole_file(self) -> None:
        reader = MAIN_SOURCE.split("static int lan_panel_read_all", 1)[1]
        body = reader[:reader.index("\n}")]
        self.assertIn("while (used < size)", body)
        self.assertIn("used += (size_t)chunk;", body)
        self.assertIn("EINTR", body)

    def test_short_file_send_is_reported_as_an_error(self) -> None:
        sender = HTTP_SOURCE.split("lan_http_send_file", 2)[-1]
        self.assertIn("if (ret == 0 && remaining > 0)", sender)
        self.assertIn("return -EIO;", sender)


class LightControlTests(unittest.TestCase):
    def test_light_control_uses_registered_gpio_device(self) -> None:
        self.assertIn('#define LED_GPIO_DEVICE "/dev/gpio2"', MAIN_SOURCE)
        self.assertRegex(
            GPIO_DRIVER_SOURCE,
            r"\{\s*2\s*,\s*GPIOD\(22\)\s*,\s*GPIO_OUTPUT_PIN\s*\}",
        )
        self.assertNotIn("CSRCS += r528_gpio.c", BOARD_MAKEFILE_SOURCE)
        self.assertIn("GPIOC_SETPINTYPE", MAIN_SOURCE)
        self.assertIn("GPIO_OUTPUT_PIN", MAIN_SOURCE)
        self.assertIn("GPIOC_WRITE", MAIN_SOURCE)

    def test_light_control_depends_on_gpio_not_pwm(self) -> None:
        block = MAIN_SOURCE.split("Light Control Thread", 1)[1]
        self.assertIn("#ifdef CONFIG_DEV_GPIO", block)
        self.assertNotIn("#ifdef CONFIG_PWM", block)

    def test_active_low_led_uses_low_time_as_brightness(self) -> None:
        self.assertIn("#define LED_ON_VALUE 0", MAIN_SOURCE)
        self.assertIn("#define LED_OFF_VALUE 1", MAIN_SOURCE)
        self.assertIn("low-duty=%d%%", MAIN_SOURCE)
        self.assertIn("int on_time_us = (cycle_us * duty_percent) / 100;",
                      MAIN_SOURCE)
        self.assertNotIn("100 - duty_percent", MAIN_SOURCE)

    def test_led_starts_off_and_requires_live_light_data(self) -> None:
        opener = MAIN_SOURCE.split("static int light_gpio_open", 1)[1]
        opener = opener[:opener.index("static int light_gpio_write")]
        self.assertIn("GPIOC_WRITE", opener)
        self.assertIn("LED_OFF_VALUE", opener)
        reader = MAIN_SOURCE.split("static int read_light_sensor", 1)[1]
        reader = reader[:reader.index("static int calculate_pwm_duty")]
        self.assertIn('"\\\"live_mask\\\":"', reader)
        self.assertIn('"\\\"stale_mask\\\":"', reader)
        self.assertIn('"\\\"offline_mask\\\":"', reader)
        self.assertIn("ROUTINE_LIGHT_MASK", reader)
        self.assertIn("buffer[nread] = '\\0';", reader)
        for name in ("live_mask", "stale_mask", "offline_mask"):
            self.assertIn('\\\"%s\\\"' % name, ROUTINE_STORAGE_SOURCE)

    def test_manual_light_test_command_is_available(self) -> None:
        self.assertIn('strcmp(argv[1], "--light") == 0', MAIN_SOURCE)
        self.assertIn('Usage: lan_panel --light <on|off>', MAIN_SOURCE)
        self.assertIn("light_manual_test(argv[2])", MAIN_SOURCE)
        manual = MAIN_SOURCE.split("static int light_manual_test", 1)[1]
        manual = manual[:manual.index("static void *light_control_thread")]
        self.assertIn("light_gpio_read(fd, &readback)", manual)
        self.assertIn("PD22 readback=%s", manual)

    def test_pd22_is_preloaded_high_before_output_registration(self) -> None:
        initialization = GPIO_DRIVER_SOURCE.split(
            "FAR struct ioexpander_dev_s *r528_gpio_initialize", 1
        )[1]
        preload = "hal_gpio_set_data(GPIOD(22), GPIO_DATA_HIGH)"
        self.assertIn(preload, initialization)
        self.assertLess(initialization.index(preload),
                        initialization.index("gpio_lower_half"))


class ConfigPostTests(unittest.TestCase):
    def test_wifi_json_uses_the_enabled_structured_parser(self) -> None:
        self.assertIn("#include <netutils/cJSON.h>", MAIN_SOURCE)
        parser = MAIN_SOURCE.split("static int lan_panel_parse_wifi_json",
                                   1)[1]
        parser = parser[:parser.index("static int lan_panel_append_wifi_line")]
        self.assertIn('cJSON_GetObjectItemCaseSensitive(root, "ssid")',
                      parser)
        self.assertIn('cJSON_GetObjectItemCaseSensitive(root, "password")',
                      parser)
        self.assertIn("cJSON_IsString(ssid)", parser)
        self.assertIn("cJSON_IsString(password)", parser)

    def test_light_json_requires_bounded_integer_values(self) -> None:
        parser = MAIN_SOURCE.split(
            "static int lan_panel_write_light_config", 1)[1]
        parser = parser[:parser.index("static bool lan_panel_safe_component")]
        self.assertIn('cJSON_GetObjectItemCaseSensitive(root, "thresh")',
                      parser)
        self.assertIn('cJSON_GetObjectItemCaseSensitive(root, "levels")',
                      parser)
        self.assertIn("thresh < 100 || thresh > 2400", parser)
        self.assertIn("levels < 3 || levels > 10", parser)

    def test_storage_failures_are_not_misreported_as_bad_json(self) -> None:
        handler = MAIN_SOURCE.split(
            '"/api/v1/wifi/config"', 1)[1]
        handler = handler[:handler.index(
            'if (strcmp(request.path, "/api/v1/history")')]
        self.assertEqual(handler.count(
            "ret == -EINVAL || ret == -E2BIG ? 400 : 500"), 3)


class PageScriptTests(unittest.TestCase):
    @staticmethod
    def script() -> str:
        page = MAIN_SOURCE.split('"<script>', 1)[1]
        return page[:page.index("</script>")]

    @staticmethod
    def source() -> str:
        return MAIN_SOURCE

    def test_headers_are_built_per_request_not_once_at_load(self) -> None:
        script = self.script()
        self.assertIn("function hdr(){return{'Authorization':'Bearer '+tok()}}",
                      script)
        self.assertNotIn("const h={'Authorization'", script)
        self.assertNotIn("{headers:h}", script)

    def test_fetches_do_not_run_before_a_token_exists(self) -> None:
        script = self.script()
        self.assertIn(
            "function refreshStatus(){if(!tok()||refreshInFlight)return;",
            script,
        )
        self.assertIn("function loadHistory(){if(!tok())return;", script)
        self.assertNotIn("setInterval(refreshStatus", script)

    def test_manual_and_configurable_refresh_controls(self) -> None:
        source = self.source()
        script = self.script()
        self.assertIn("id=refreshNow", source)
        self.assertIn("onclick='refreshStatus()'", source)
        self.assertIn("id=refreshInterval", source)
        for seconds in (0, 2, 5, 10, 30, 60):
            self.assertIn("<option value=%d" % seconds, source)
        self.assertIn("function setRefreshInterval(v)", script)
        self.assertIn("clearTimeout(refreshTimer)", script)
        self.assertIn("setTimeout(refreshStatus,parseInt(v,10)*1000)", script)
        self.assertIn("localStorage.setItem('refreshSeconds',v)", script)
        self.assertIn("refreshInFlight", script)
        self.assertIn("button.disabled=true", script)
        self.assertIn("button.disabled=false", script)

    def test_unauthorized_clears_the_stale_token(self) -> None:
        script = self.script()
        self.assertIn("if(r.status==401)", script)
        self.assertIn("sessionStorage.removeItem('token')", script)

    def test_status_requests_bypass_the_browser_cache(self) -> None:
        self.assertIn("cache:'no-store'", self.script())

    def test_failed_responses_surface_the_status_code(self) -> None:
        script = self.script()
        self.assertIn("if(!r.ok)throw new Error('HTTP '+r.status)", script)

    def test_history_tolerates_a_partial_trailing_line(self) -> None:
        script = self.script()
        self.assertIn("filter(l=>l)", script)
        self.assertIn("try{const r=JSON.parse(l)", script)
        self.assertIn("catch(e){}", script)

    def test_history_is_fetched_fresh_and_not_truncated(self) -> None:
        script = self.script()
        self.assertIn("function fetchHistoryText(){return get('/api/v1/history')",
                      script)
        self.assertIn("function loadHistory(){if(!tok())return;", script)
        self.assertIn("fetchHistoryText().then", script)
        self.assertNotIn("slice(-50)", script)
        self.assertIn("加载全部历史", MAIN_SOURCE)

    def test_curve_interval_and_sd_export_controls_are_served(self) -> None:
        source = self.source()
        script = self.script()
        self.assertIn("id=curveModal", source)
        self.assertIn("id=curveCanvas", source)
        for seconds in (5, 10, 60, 300, 600, 1800, 3600):
            self.assertIn("<option value=%d" % seconds, source)
        self.assertIn("confirm('确认将全部历史数据导出到SD卡吗？')", script)
        self.assertIn("'/api/v1/history/export'", script)

    def test_history_uses_calendar_table_instead_of_raw_json_log(self) -> None:
        source = self.source()
        script = self.script()
        self.assertIn("class=data-table-wrap id=history", source)
        self.assertIn("function historyDate(r)", script)
        self.assertIn("d.toISOString().slice(0,10)", script)
        self.assertIn("function renderDataTable(rows,targetId)", script)
        for column in ("日期", "时间", "温度", "湿度", "光照", "eCO₂", "TVOC"):
            self.assertIn("'%s'" % column, script)
        self.assertNotIn("JSON.stringify(JSON.parse(l),null,2)", script)
        self.assertNotIn("history-log", source)

    def test_curve_exposes_every_point_value(self) -> None:
        source = self.source()
        script = self.script()
        self.assertIn("id=curveTooltip", source)
        self.assertIn("id=curvePoints", source)
        self.assertIn("x.arc(px,py,2.8", script)
        self.assertIn("function curvePointer(e)", script)
        self.assertIn("renderDataTable(historyRows,'curvePoints')", script)

    def test_safe_config_posts_retry_only_network_failures(self) -> None:
        script = self.script()
        self.assertIn("function networkError(e)", script)
        self.assertIn("function retryFetch(u,o,retries)", script)
        self.assertIn("function configPost(u,body)", script)
        self.assertIn("configPost('/api/v1/routine/config'", script)
        self.assertIn("configPost('/api/v1/light/config'", script)
        self.assertNotIn("retryFetch('/api/v1/history/export'", script)

    def test_wifi_post_declares_its_json_content_type(self) -> None:
        self.assertIn("'Content-Type':'application/json'", self.script())

    def test_dom_writes_never_use_the_implicit_id_globals(self) -> None:
        """window.status and window.history shadow those ids, so a bare
        `status.textContent=` write is discarded by the browser."""

        script = self.script()
        self.assertIn("function el(i){return document.getElementById(i)}",
                      script)
        for shadowed in ("status", "history"):
            self.assertNotIn("%s.textContent=" % shadowed, script)
            self.assertIn("el('%s').textContent=" % shadowed, script)

    def test_json_reply_lengths_come_from_the_literal(self) -> None:
        """Hand-counted lengths overran {"ok":true} and the trailing bytes
        made the page's JSON.parse fail at position 13."""

        source = self.source()
        self.assertIn("reply = ret < 0 ? rejected : accepted;", source)
        self.assertIn("reply, strlen(reply)", source)
        self.assertNotIn("ret < 0 ? 15 : 14", source)

    def test_favicon_is_answered_without_authorization(self) -> None:
        source = self.source()
        favicon = source.index('"/favicon.ico"')
        checker = source.index("lan_panel_authorized(&request)")
        self.assertLess(favicon, checker)
        self.assertIn('204, "image/x-icon", NULL, 0', source)

    def test_absent_logs_report_an_empty_feed_not_404(self) -> None:
        source = self.source()
        for path, macro in (("history", "LAN_PANEL_HISTORY"),
                            ("events", "LAN_PANEL_EVENTS")):
            self.assertIn("access(%s, R_OK) < 0" % macro, source)
            self.assertIn('"/api/v1/%s"' % path, source)
        self.assertIn('200, "application/x-ndjson",\n                        '
                      '                NULL, 0', source)


class HistoryExportTests(unittest.TestCase):
    def test_export_automounts_the_fat_sd_card(self) -> None:
        self.assertIn('#define LAN_PANEL_SD_DEVICE "/dev/mmcsd1"',
                      MAIN_SOURCE)
        self.assertIn('#define LAN_PANEL_SD_MOUNT "/sdcard"', MAIN_SOURCE)
        prepare = MAIN_SOURCE.split("static int lan_panel_prepare_sd_mount",
                                    1)[1]
        prepare = prepare[:prepare.index("static int lan_panel_export_history")]
        self.assertIn("mount(LAN_PANEL_SD_DEVICE, LAN_PANEL_SD_MOUNT,",
                      prepare)
        self.assertIn("FATFS_SUPER_MAGIC", MAIN_SOURCE)
        self.assertIn("MSDOS_SUPER_MAGIC", MAIN_SOURCE)

    def test_export_never_overwrites_an_existing_file(self) -> None:
        export = MAIN_SOURCE.split("static int lan_panel_export_history",
                                   1)[1]
        export = export[:export.index("static bool lan_panel_safe_component")]
        self.assertIn("O_WRONLY | O_CREAT | O_EXCL", export)
        self.assertIn("/routine-history-%ld", export)
        self.assertIn("fsync(output)", export)


if __name__ == "__main__":
    unittest.main()
