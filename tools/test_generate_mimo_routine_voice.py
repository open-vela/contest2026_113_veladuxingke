#!/usr/bin/env python3
"""Tests for the interactive MiMo generation launcher."""

import importlib.util
import os
import subprocess
import unittest
from pathlib import Path
from unittest import mock

MODULE_PATH = Path(__file__).with_name("generate_mimo_routine_voice.py")
SPEC = importlib.util.spec_from_file_location("generate_mimo_routine_voice", MODULE_PATH)
launcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(launcher)


class PromptTests(unittest.TestCase):
    def test_tty_uses_getpass(self) -> None:
        with mock.patch.object(launcher.sys.stdin, "isatty", return_value=True), mock.patch.object(
            launcher.getpass, "getpass", return_value="  secret-key  "
        ):
            self.assertEqual(launcher.prompt_for_key(), "secret-key")

    def test_non_tty_uses_zenity_password_dialog(self) -> None:
        completed = subprocess.CompletedProcess([], 0, "secret-key\n", "")
        environment = {"DISPLAY": ":0"}
        with mock.patch.object(launcher.sys.stdin, "isatty", return_value=False), mock.patch.object(
            launcher.shutil, "which", return_value="/usr/bin/zenity"
        ), mock.patch.object(launcher.subprocess, "run", return_value=completed) as run, mock.patch.dict(
            os.environ, environment, clear=True
        ):
            self.assertEqual(launcher.prompt_for_key(), "secret-key")
        arguments = run.call_args.args[0]
        self.assertIn("--password", arguments)
        self.assertNotIn("secret-key", arguments)

    def test_cancel_returns_empty(self) -> None:
        completed = subprocess.CompletedProcess([], 1, "", "")
        with mock.patch.object(launcher.sys.stdin, "isatty", return_value=False), mock.patch.object(
            launcher.shutil, "which", return_value="/usr/bin/zenity"
        ), mock.patch.object(launcher.subprocess, "run", return_value=completed), mock.patch.dict(
            os.environ, {"WAYLAND_DISPLAY": "wayland-0"}, clear=True
        ):
            self.assertEqual(launcher.prompt_for_key(), "")

    def test_no_secure_prompt_fails_without_reading_stdin(self) -> None:
        with mock.patch.object(launcher.sys.stdin, "isatty", return_value=False), mock.patch.object(
            launcher.shutil, "which", return_value=None
        ), mock.patch.dict(os.environ, {}, clear=True):
            with self.assertRaises(RuntimeError):
                launcher.prompt_for_key()


if __name__ == "__main__":
    unittest.main()
