#!/usr/bin/env python3
"""Interactive launcher for an explicitly selected MiMo ASR corpus baseline."""

import getpass
import os
import secrets
import shutil
import subprocess
import sys
from pathlib import Path

BASELINE = Path(__file__).with_name("mimo_asr_baseline.py")


def prompt_for_consent(sample_ids: list[str]) -> bool:
    print("以下 R528 DMIC 录音将上传到 Xiaomi MiMo API：")
    for sample_id in sample_ids:
        print(f"- {sample_id}")
    answer = input("输入 UPLOAD 确认上传；其他输入均取消：").strip()
    return answer == "UPLOAD"


def prompt_for_key() -> str:
    if sys.stdin.isatty():
        return getpass.getpass("请输入 MiMo API key（输入时不会显示）：").strip()
    zenity = shutil.which("zenity")
    if zenity and (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        result = subprocess.run(
            [
                zenity,
                "--password",
                "--title=MiMo API key",
                "--text=所选录音将上传到 MiMo。API key 不会写入仓库。",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        if result.returncode == 0:
            return result.stdout.rstrip("\r\n")
        if result.returncode == 1:
            return ""
    raise RuntimeError("没有可用的安全 API key 输入界面")


def create_key_file(key: str) -> Path:
    runtime_dir = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
    info = runtime_dir.stat()
    if info.st_uid != os.getuid() or info.st_mode & 0o022:
        raise RuntimeError(f"不安全的运行时目录：{runtime_dir}")
    path = runtime_dir / f"mimo-asr-key-{os.getpid()}-{secrets.token_hex(8)}"
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as target:
            target.write(key + "\n")
            target.flush()
            os.fsync(target.fileno())
    except BaseException:
        path.unlink(missing_ok=True)
        raise
    return path


def main() -> int:
    if len(sys.argv) < 3:
        print(
            f"用法：{Path(sys.argv[0]).name} CORPUS_DIR SAMPLE_ID [SAMPLE_ID ...]",
            file=sys.stderr,
        )
        return 2
    corpus = Path(sys.argv[1]).expanduser()
    sample_ids = sys.argv[2:]
    if not prompt_for_consent(sample_ids):
        print("未确认上传，已取消。")
        return 1
    try:
        key = prompt_for_key()
    except (EOFError, OSError, RuntimeError) as error:
        print(f"无法安全读取 API key：{error}", file=sys.stderr)
        return 1
    if not key:
        print("未输入 API key，已取消。", file=sys.stderr)
        return 1

    key_file = None
    try:
        key_file = create_key_file(key)
        key = ""
        command = [
            sys.executable,
            str(BASELINE),
            "--key-file",
            str(key_file),
            "--corpus",
            str(corpus),
        ]
        for sample_id in sample_ids:
            command.extend(["--sample", sample_id])
        return subprocess.run(command, check=False).returncode
    except (OSError, RuntimeError) as error:
        print(f"启动失败：{error}", file=sys.stderr)
        return 1
    finally:
        key = ""
        if key_file is not None:
            key_file.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
