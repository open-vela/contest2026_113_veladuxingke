#!/usr/bin/env python3
"""Static UI contracts for the 480x320 routine manager layout."""

import re
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
UI_PATH = PROJECT_ROOT / "app" / "routine_manager" / "routine_ui.c"
LAYOUT_PATH = PROJECT_ROOT / "app" / "routine_manager" / "routine_ui_layout.h"
SOURCE = UI_PATH.read_text(encoding="utf-8")
LAYOUT_SOURCE = LAYOUT_PATH.read_text(encoding="utf-8")
WIFI_UI_SOURCE = (PROJECT_ROOT / "app" / "routine_manager" /
                  "wifi_ui.c").read_text(encoding="utf-8")
WIFI_MONITOR_SOURCE = (PROJECT_ROOT / "app" / "routine_manager" /
                       "wifi_status_monitor.c").read_text(encoding="utf-8")
STORAGE_SOURCE = (PROJECT_ROOT / "app" / "routine_manager" /
                  "routine_storage.c").read_text(encoding="utf-8")


def layout_value(name: str) -> int:
    match = re.search(rf"^#define\s+{name}\s+(-?\d+)\s*$", LAYOUT_SOURCE, re.M)
    if match is None:
        raise AssertionError(f"missing layout constant {name}")
    return int(match.group(1))


class RoutineUiContractTests(unittest.TestCase):
    def test_wifi_connect_stays_on_wifi_page(self) -> None:
        callback = WIFI_UI_SOURCE.split("static void wifi_ui_connect_clicked",
                                        1)[1]
        callback = callback[:callback.index("static void wifi_ui_cancel_clicked")]
        self.assertIn("lv_obj_add_flag(ctx->password_panel",
                      callback)
        self.assertNotIn("wifi_ui_hide", callback)
        self.assertNotIn("lv_obj_add_flag(ctx->panel", callback)

    def test_wifi_password_keyboard_accepts_letters_and_symbols(self) -> None:
        self.assertIn("LV_KEYBOARD_MODE_TEXT_LOWER", WIFI_UI_SOURCE)
        self.assertNotIn("lv_keyboard_set_mode(ctx->keyboard, "
                         "LV_KEYBOARD_MODE_NUMBER)", WIFI_UI_SOURCE)

    def test_connected_popup_and_later_token_redisplay(self) -> None:
        self.assertIn("网页: http://%s:8080/", WIFI_UI_SOURCE)
        self.assertIn("4位令牌: %s", WIFI_UI_SOURCE)
        self.assertIn("wifi_ui_show_connection_info", WIFI_MONITOR_SOURCE)
        button = SOURCE.split("static void routine_wifi_button_cb", 1)[1]
        button = button[:button.index("static void routine_wifi_connect_cb")]
        self.assertIn("wifi_status_monitor_check_now()", button)

    def test_wifi_symbol_uses_the_builtin_symbol_font(self) -> None:
        button = SOURCE.split("static lv_obj_t *routine_create_small_button",
                              1)[1]
        button = button[:button.index("static void routine_wifi_button_cb")]
        self.assertIn("strcmp(text, LV_SYMBOL_WIFI) == 0", button)
        self.assertIn("&lv_font_montserrat_14", button)
        self.assertIn("lv_obj_set_style_text_font(icon, &lv_font_montserrat_14",
                      WIFI_UI_SOURCE)

    def test_device_history_export_automounts_sd_card(self) -> None:
        prepare = STORAGE_SOURCE.split(
            "static int routine_storage_prepare_export_mount", 1
        )[1]
        prepare = prepare[:prepare.index("static int routine_storage_write_all")]
        self.assertIn("mount(ROUTINE_STORAGE_EXPORT_DEVICE", prepare)
        self.assertIn("routine_storage_export_mount_ready()", prepare)

    def test_navigation_does_not_extend_into_cards(self) -> None:
        self.assertNotIn("lv_obj_set_ext_click_area(g_ui.page_", SOURCE)
        header = layout_value("ROUTINE_UI_HEADER_HEIGHT")
        button_y = layout_value("ROUTINE_UI_NAV_BUTTON_Y")
        button_w = layout_value("ROUTINE_UI_NAV_BUTTON_WIDTH")
        button_h = layout_value("ROUTINE_UI_NAV_BUTTON_HEIGHT")
        card_left = layout_value("ROUTINE_UI_CARD_LEFT_X")
        card_right = layout_value("ROUTINE_UI_CARD_RIGHT_X")
        card_first = layout_value("ROUTINE_UI_CARD_FIRST_Y") + header
        card_second = layout_value("ROUTINE_UI_CARD_SECOND_Y") + header
        card_w = layout_value("ROUTINE_UI_CARD_WIDTH")
        card_h = layout_value("ROUTINE_UI_CARD_HEIGHT")
        buttons = {
            "previous": (layout_value("ROUTINE_UI_NAV_PREV_X"), button_y,
                         button_w, button_h),
            "next": (layout_value("ROUTINE_UI_NAV_NEXT_X"), button_y,
                     button_w, button_h),
        }
        cards = [
            (card_left, card_first, card_w, card_h),
            (card_right, card_first, card_w, card_h),
            (card_left, card_second, card_w, card_h),
            (card_right, card_second, card_w, card_h),
        ]
        self.assertGreaterEqual(button_w, 44)
        self.assertGreaterEqual(button_h, 32)
        self.assertGreaterEqual(
            card_first - (button_y + button_h),
            layout_value("ROUTINE_UI_NAV_CARD_GAP"),
        )
        for name, button in buttons.items():
            bx, by, bw, bh = button
            self.assertGreaterEqual(bx, 0, name)
            self.assertGreaterEqual(by, 0, name)
            self.assertLessEqual(bx + bw, 480, name)
            self.assertLessEqual(by + bh, 320, name)
            for card in cards:
                cx, cy, cw, ch = card
                intersects = not (
                    bx + bw <= cx or cx + cw <= bx or
                    by + bh <= cy or cy + ch <= by
                )
                self.assertFalse(intersects, f"{name} intersects {card}")

    def test_button_navigation_is_instant_and_debounced(self) -> None:
        callback = re.search(
            r"static void routine_page_button_cb.*?\n}\n", SOURCE, re.S
        )
        self.assertIsNotNone(callback)
        self.assertIn("LV_ANIM_OFF", callback.group(0))
        self.assertNotIn("LV_ANIM_ON", callback.group(0))
        self.assertIn("lv_tick_get()", callback.group(0))
        self.assertIn("< 500", callback.group(0))
        self.assertIn("LV_EVENT_SHORT_CLICKED", SOURCE)
        self.assertIn("lv_tileview_create", SOURCE)

    def test_sensor_cards_have_independent_requests(self) -> None:
        self.assertIn("ROUTINE_UI_DIALOG_TEMPERATURE", SOURCE)
        self.assertIn("ROUTINE_UI_DIALOG_HUMIDITY", SOURCE)
        self.assertIn("AUDIO_SERVICE_REQUEST_TEMPERATURE", SOURCE)
        self.assertIn("AUDIO_SERVICE_REQUEST_HUMIDITY", SOURCE)
        self.assertNotIn("AUDIO_SERVICE_REQUEST_SHT40", SOURCE)

    def test_numeric_labels_use_ascii_unavailable_marker(self) -> None:
        self.assertIn('snprintf(g_ui.value_buffer[i], sizeof(g_ui.value_buffer[i]), "--")', SOURCE)
        self.assertNotRegex(
            SOURCE,
            r'value_buffer\[i\].*g_ui\.text->status',
        )

    def test_chart_has_no_forced_full_refresh(self) -> None:
        self.assertNotIn("lv_chart_refresh(g_ui.chart)", SOURCE)
        self.assertIn("routine_chart_visible_range", SOURCE)
        self.assertIn("history_follow_latest", SOURCE)

    def test_chart_shows_seconds_and_value_for_every_visible_point(self) -> None:
        self.assertIn("temperature_history_second", SOURCE)
        self.assertIn("chart_value[ROUTINE_CHART_POINTS]", SOURCE)
        self.assertIn("chart_value_buffer[ROUTINE_CHART_POINTS]", SOURCE)
        self.assertIn('"%02u:%02u:%02u"', SOURCE)
        self.assertIn("routine_format_signed_x100(g_ui.chart_value_buffer[i]",
                      SOURCE)

    def test_history_interval_includes_five_and_ten_seconds(self) -> None:
        self.assertIn("{5, 10, 60, 300, 600, 1800, 3600}", SOURCE)
        self.assertIn('seconds < 60', SOURCE)


if __name__ == "__main__":
    unittest.main()
