#!/usr/bin/env python3
"""Check that runtime voice references, manifest, and ROMFS agree."""

import hashlib
import json
import re
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
VOICE_DIR = (
    PROJECT_ROOT / "board" / "r528s3-dshanpi" / "src" / "etc" /
    "routine_voice"
)
MANIFEST_PATH = VOICE_DIR / "assets.json"
VOICE_REPLY_PATH = PROJECT_ROOT / "app" / "routine_manager" / "voice_reply.c"
BOARD_MAKEFILE_PATH = (
    PROJECT_ROOT / "board" / "r528s3-dshanpi" / "src" / "Makefile"
)
PCM_LITERAL = re.compile(r'"([A-Za-z0-9_]+\.pcm)"')
PACKED_PCM = re.compile(r"etc/routine_voice/([A-Za-z0-9_]+\.pcm)")


class VoiceAssetContractTests(unittest.TestCase):
    def test_manifest_romfs_and_c_references_agree(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        assets = manifest["assets"]
        runtime_files = []

        for name, metadata in assets.items():
            runtime_file = metadata.get("runtime_file")
            self.assertIsInstance(runtime_file, str, name)
            self.assertEqual(Path(runtime_file).name, runtime_file, name)
            self.assertTrue(runtime_file.endswith(".pcm"), name)
            self.assertLessEqual(len(runtime_file.encode("utf-8")), 32, name)
            self.assertTrue(runtime_file.isascii(), name)
            path = VOICE_DIR / runtime_file
            self.assertTrue(path.is_file(), runtime_file)
            data = path.read_bytes()
            self.assertEqual(metadata["bytes"], len(data), runtime_file)
            self.assertEqual(
                metadata["output_sha256"], hashlib.sha256(data).hexdigest(),
                runtime_file,
            )
            runtime_files.append(runtime_file)

        self.assertEqual(len(runtime_files), len(set(runtime_files)))
        self.assertEqual(
            len(runtime_files), len({name.casefold() for name in runtime_files})
        )
        self.assertIn("eco2_intro.pcm", runtime_files)
        self.assertIn("tvoc_intro.pcm", runtime_files)
        self.assertNotIn("carbon_dioxide_concentration_is.pcm", runtime_files)
        self.assertNotIn(
            "volatile_organic_compound_concentration_is.pcm", runtime_files
        )
        romfs_files = {path.name for path in VOICE_DIR.glob("*.pcm")}
        self.assertEqual(set(runtime_files), romfs_files)

        source = VOICE_REPLY_PATH.read_text(encoding="utf-8")
        c_references = set(PCM_LITERAL.findall(source))
        self.assertTrue(c_references)
        self.assertTrue(c_references.issubset(romfs_files))
        self.assertIn("eco2_intro.pcm", c_references)
        self.assertIn("tvoc_intro.pcm", c_references)
        self.assertNotIn("wake_ack.pcm", c_references)

        makefile = BOARD_MAKEFILE_PATH.read_text(encoding="utf-8")
        packed_files = set(PACKED_PCM.findall(makefile))
        self.assertEqual(c_references, packed_files)
        self.assertNotIn("assets.json", packed_files)
        self.assertNotIn("schedule_reminder.pcm", packed_files)
        self.assertNotIn("wake_ack.pcm", packed_files)


if __name__ == "__main__":
    unittest.main()
