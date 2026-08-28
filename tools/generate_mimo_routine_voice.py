#!/usr/bin/env python3
"""One-command interactive launcher for MiMo routine voice generation."""

import getpass
import os
import secrets
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
GENERATOR = Path(__file__).with_name("generate_mimo_voice_wavs.py")
DEFAULT_OUTPUT = Path.home() / "mimo-routine-voice-staging"


def create_key_file(key: str) -> Path:
    runtime_dir = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
    info = runtime_dir.stat()
    permissions = info.st_mode & 0o777
    if info.st_uid != os.getuid() or permissions & 0o022:
        raise RuntimeError(f"不安全的运行时目录：{runtime_dir}")
    path = runtime_dir / f"mimo-api-key-{os.getpid()}-{secrets.token_hex(8)}"
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            output.write(key)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
    except BaseException:
        path.unlink(missing_ok=True)
        raise
    return path


def prompt_for_key() -> str:
    if sys.stdin.isatty():
        return getpass.getpass(
            "请输入 MiMo API key（输入时屏幕不会显示）："
        ).strip()

    zenity = shutil.which("zenity")
    if zenity and (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        result = subprocess.run(
            [
                zenity,
                "--password",
                "--title=MiMo API key",
                "--text=请输入 MiMo API key。内容不会显示，也不会写入仓库。",
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
        raise RuntimeError("无法打开安全的 API key 输入窗口")

    raise RuntimeError(
        "当前命令没有交互终端，也没有可用的图形密码框；请在普通终端运行此脚本"
    )


def main() -> int:
    output = Path(sys.argv[1]).expanduser() if len(sys.argv) == 2 else DEFAULT_OUTPUT
    if len(sys.argv) > 2:
        print(f"用法：{Path(sys.argv[0]).name} [输出目录]", file=sys.stderr)
        return 2

    print("MiMo V2.5 TTS 固定语音生成器")
    print("- 模型：mimo-v2.5-tts")
    print("- 音色：冰糖")
    print(f"- 输出：{output}")
    print("- API key 仅隐藏输入并暂存于 /run/user，程序结束后自动删除。")
    print("- key 不会显示，也不会写入仓库、命令行参数或生成清单。")
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
            str(GENERATOR),
            "--key-file",
            str(key_file),
            "--output",
            str(output),
        ]
        result = subprocess.run(command, cwd=PROJECT_ROOT, check=False)
        return result.returncode
    except (OSError, RuntimeError) as error:
        print(f"启动失败：{error}", file=sys.stderr)
        return 1
    finally:
        key = ""
        if key_file is not None:
            try:
                key_file.unlink(missing_ok=True)
            except OSError as error:
                print(f"警告：临时 key 文件删除失败：{key_file} ({error})", file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
