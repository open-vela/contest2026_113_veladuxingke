#!/usr/bin/env python3
"""Regression tests for the R528 Codec mixed RW/W1C RAMP register."""

import re
import unittest
from pathlib import Path
from typing import Optional, Tuple

PROJECT_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT_ROOT.parent
CODEC_DIR = (
    WORKSPACE
    / "vendor/allwinnertech/chips/r528/drivers/rtos-hal/hal/source/sound/codecs"
)
HEADER = (CODEC_DIR / "sun8iw20-codec.h").read_text(encoding="utf-8")
SOURCE = (CODEC_DIR / "sun8iw20-codec.c").read_text(encoding="utf-8")
CORE_HEADER = (
    WORKSPACE
    / "vendor/allwinnertech/chips/r528/drivers/rtos-hal/include/hal/sound/snd_core.h"
).read_text(encoding="utf-8")

RAMP_RISE_INT = 30
RAMP_FALL_INT = 28
RAMP_W1C_MASK = (1 << RAMP_RISE_INT) | (1 << RAMP_FALL_INT)
RMCEN = 1 << 1
RDEN = 1 << 0
RW_MASK = 0xFFFFFFFF & ~RAMP_W1C_MASK
RAMP_FIELDS = 0x00180000


def generic_update(old: int, mask: int, value: int) -> int:
    """Return the payload produced by the unsafe generic read/modify/write."""
    return (old & ~mask) | (value & mask)


def safe_update(old: int, mask: int, value: int) -> Optional[int]:
    """Model sunxi_codec_ramp_update_bits; None means no hardware write."""
    if mask & RAMP_W1C_MASK:
        raise ValueError("control mask contains W1C status bits")

    new = ((old & ~mask) | (value & mask)) & ~RAMP_W1C_MASK
    if (old & mask) == (new & mask):
        return None
    return new


def acknowledge(old: int, status: int) -> int:
    """Return the W1C acknowledgement payload used by the Codec helper."""
    status &= RAMP_W1C_MASK
    return (old & ~RAMP_W1C_MASK) | status


def hardware_write(old: int, payload: int) -> int:
    """Apply mixed-register semantics: ordinary RW plus write-one-to-clear."""
    rw_value = payload & RW_MASK
    status_value = (old & RAMP_W1C_MASK) & ~(payload & RAMP_W1C_MASK)
    return rw_value | status_value


def control_write(old: int, mask: int, value: int) -> Tuple[int, Optional[int]]:
    payload = safe_update(old, mask, value)
    if payload is None:
        return old, None
    return hardware_write(old, payload), payload


def acknowledge_write(old: int, status: int) -> Tuple[int, int]:
    payload = acknowledge(old, status)
    return hardware_write(old, payload), payload


class RampRegisterModelTests(unittest.TestCase):
    def test_generic_rmw_writes_back_and_clears_rise_status(self) -> None:
        old = RAMP_FIELDS | RDEN | (1 << RAMP_RISE_INT)
        payload = generic_update(old, RDEN, 0)

        self.assertEqual(payload, 0x40180000)
        self.assertTrue(payload & RAMP_W1C_MASK)
        self.assertEqual(hardware_write(old, payload), RAMP_FIELDS)

    def test_safe_control_write_never_carries_w1c_payload(self) -> None:
        old = RAMP_FIELDS | RAMP_W1C_MASK
        updated, payload = control_write(old, RDEN, RDEN)

        self.assertEqual(payload, RAMP_FIELDS | RDEN)
        self.assertEqual(payload & RAMP_W1C_MASK, 0)
        self.assertEqual(updated & RAMP_W1C_MASK, RAMP_W1C_MASK)

    def test_explicit_ack_clears_only_requested_completion(self) -> None:
        old = RAMP_FIELDS | RDEN | RAMP_W1C_MASK
        updated, payload = acknowledge_write(old, 1 << RAMP_RISE_INT)

        self.assertEqual(payload, RAMP_FIELDS | RDEN | (1 << RAMP_RISE_INT))
        self.assertEqual(updated & (1 << RAMP_RISE_INT), 0)
        self.assertNotEqual(updated & (1 << RAMP_FALL_INT), 0)
        self.assertEqual(updated & (RAMP_FIELDS | RDEN), RAMP_FIELDS | RDEN)

    def test_safe_rmcen_update_preserves_unrelated_rw_fields(self) -> None:
        old = RAMP_FIELDS | RDEN | RAMP_W1C_MASK
        updated, payload = control_write(old, RMCEN, RMCEN)

        self.assertEqual(payload, RAMP_FIELDS | RMCEN | RDEN)
        self.assertEqual(updated & RAMP_FIELDS, RAMP_FIELDS)
        self.assertEqual(updated & (RMCEN | RDEN), RMCEN | RDEN)
        self.assertEqual(updated & RAMP_W1C_MASK, RAMP_W1C_MASK)

    def test_no_target_change_skips_control_write(self) -> None:
        old = RAMP_FIELDS | RDEN | RAMP_W1C_MASK
        updated, payload = control_write(old, RDEN, RDEN)

        self.assertIsNone(payload)
        self.assertEqual(updated, old)

    def test_rejects_w1c_bits_in_control_mask(self) -> None:
        with self.assertRaises(ValueError):
            safe_update(RAMP_FIELDS, RDEN | (1 << RAMP_RISE_INT), RDEN)

    def test_twenty_cycles_ack_each_completion_and_rearm(self) -> None:
        register = RAMP_FIELDS
        control_payloads = []
        ack_payloads = []

        for _ in range(20):
            register, payload = acknowledge_write(register, RAMP_W1C_MASK)
            ack_payloads.append(payload)
            register, payload = control_write(register, RMCEN, 0)
            if payload is not None:
                control_payloads.append(payload)
            register, payload = control_write(register, RDEN, RDEN)
            self.assertIsNotNone(payload)
            control_payloads.append(payload)
            register |= 1 << RAMP_RISE_INT
            register, payload = acknowledge_write(register, 1 << RAMP_RISE_INT)
            ack_payloads.append(payload)
            self.assertEqual(register & (1 << RAMP_RISE_INT), 0)

            register, payload = acknowledge_write(register, RAMP_W1C_MASK)
            ack_payloads.append(payload)
            register, payload = control_write(register, RDEN, 0)
            self.assertIsNotNone(payload)
            control_payloads.append(payload)
            register |= 1 << RAMP_FALL_INT
            register, payload = acknowledge_write(register, 1 << RAMP_FALL_INT)
            ack_payloads.append(payload)
            self.assertEqual(register & (1 << RAMP_FALL_INT), 0)

        self.assertEqual(len(control_payloads), 40)
        self.assertTrue(
            all(payload & RAMP_W1C_MASK == 0 for payload in control_payloads)
        )
        self.assertTrue(any(payload & RAMP_W1C_MASK for payload in ack_payloads))
        self.assertEqual(register & RAMP_W1C_MASK, 0)
        self.assertEqual(register & RAMP_FIELDS, RAMP_FIELDS)


class RampProductionContractTests(unittest.TestCase):
    def test_header_defines_documented_ramp_interrupt_fields(self) -> None:
        self.assertRegex(HEADER, r"#define\s+RAMP_RISE_INT_EN\s+31\b")
        self.assertRegex(HEADER, r"#define\s+RAMP_RISE_INT\s+30\b")
        self.assertRegex(HEADER, r"#define\s+RAMP_FALL_INT_EN\s+29\b")
        self.assertRegex(HEADER, r"#define\s+RAMP_FALL_INT\s+28\b")
        self.assertRegex(HEADER, r"#define\s+RAMP_W1C_MASK\b")
        self.assertRegex(HEADER, r"#define\s+RMCEN\s+1\b")
        self.assertRegex(HEADER, r"#define\s+RDEN\s+0\b")

    def test_control_helper_filters_w1c_bits_and_skips_noop(self) -> None:
        helper = re.search(
            r"static void sunxi_codec_ramp_update_bits\(.*?\n}\n",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(helper)
        helper_source = helper.group(0)
        self.assertIn("mask & RAMP_W1C_MASK", helper_source)
        self.assertIn("& ~RAMP_W1C_MASK", helper_source)
        self.assertIn("(old & mask) != (new & mask)", helper_source)
        self.assertIn(
            "snd_codec_write(codec, SUNXI_RAMP_ANA_CTL, new)", helper_source
        )

    def test_ack_helper_preserves_rw_and_writes_requested_status(self) -> None:
        helper = re.search(
            r"static void sunxi_codec_ramp_ack\(.*?\n}\n", SOURCE, re.S
        )
        self.assertIsNotNone(helper)
        helper_source = helper.group(0)
        self.assertIn("status &= RAMP_W1C_MASK", helper_source)
        self.assertIn("(value & ~RAMP_W1C_MASK) | status", helper_source)
        self.assertIn(
            "snd_codec_write(codec, SUNXI_RAMP_ANA_CTL, value)", helper_source
        )

    def test_wait_helper_acks_rise_and_fall_with_timeout(self) -> None:
        helper = re.search(
            r"static void sunxi_codec_ramp_wait_and_ack\(.*?\n}\n",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(helper)
        helper_source = helper.group(0)
        self.assertIn("retries < 120", helper_source)
        self.assertIn("hal_msleep(1)", helper_source)
        self.assertIn("sunxi_codec_ramp_ack(codec, status)", helper_source)
        self.assertIn("ramp completion timeout", helper_source)
        self.assertIn("0x1<<RAMP_RISE_INT", SOURCE)
        self.assertIn("0x1<<RAMP_FALL_INT", SOURCE)

    def test_direct_mmio_write_return_is_not_treated_as_errno(self) -> None:
        self.assertIn(
            "#define snd_writel(value,reg)   (*(volatile uint32_t *)(reg) = (value))",
            CORE_HEADER,
        )
        self.assertNotIn("ramp register write failed", SOURCE)

    def test_all_ramp_control_writes_avoid_generic_updater(self) -> None:
        generic_ramp_write = re.compile(
            r"snd_codec_update_bits\s*\(\s*codec\s*,\s*SUNXI_RAMP_ANA_CTL",
            re.S,
        )
        self.assertNotRegex(SOURCE, generic_ramp_write)

    def test_non_a_headphone_sequence_matches_reference_order(self) -> None:
        route = re.search(
            r"static void sunxi_codec_playback_hp_route\(.*?\n}\n",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(route)
        route_source = route.group(0)
        self.assertLess(
            route_source.index("(0x1<<RMCEN), (0x0<<RMCEN)"),
            route_source.index("(0x1<<RDEN), (0x1<<RDEN)"),
        )
        rden_off = route_source.index("(0x1<<RDEN), (0x0<<RDEN)")
        self.assertLess(
            rden_off,
            route_source.index("(0x1<<DACLEN) | (0x1<<DACREN)", rden_off),
        )

    def test_lineout_uses_rmcen_when_headphone_ramp_is_disabled(self) -> None:
        route = re.search(
            r"static void sunxi_codec_playback_lineout_route\(.*?\n}\n",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(route)
        route_source = route.group(0)
        self.assertGreaterEqual(route_source.count("if (!(reg_val & (0x1 << RDEN)))"), 2)
        self.assertIn("0x1<<RMCEN, 0x1<<RMCEN", route_source)
        self.assertIn("0x1<<RMCEN, 0x0<<RMCEN", route_source)

    def test_combined_route_stops_headphone_before_lineout(self) -> None:
        case = re.search(
            r"case PB_AUDIO_ROUTE_LO_HP_SPK:.*?break;", SOURCE, re.S
        )
        self.assertIsNotNone(case)
        source = case.group(0)
        off = source[source.index("} else {") :]
        self.assertLess(
            off.index("sunxi_codec_playback_hp_route(codec, 1, 0)"),
            off.index("sunxi_codec_playback_lineout_route(codec, 0, 0)"),
        )


if __name__ == "__main__":
    unittest.main()
