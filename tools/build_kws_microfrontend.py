#!/usr/bin/env python3
"""Build the host shared library for the R528 fixed-point KWS frontend."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TFLM = ROOT / "apps/mlearning/tflite-micro/tflite-micro"
FRONTEND = TFLM / "tensorflow/lite/experimental/microfrontend/lib"
KISSFFT = ROOT / "apps/math/kissfft/kissfft"
BRIDGE = ROOT / "contest2026_113_veladuxingke/app/routine_manager/kws_frontend.cc"

C_SOURCES = (
    "frontend.c",
    "frontend_util.c",
    "filterbank.c",
    "filterbank_util.c",
    "log_lut.c",
    "log_scale.c",
    "log_scale_util.c",
    "noise_reduction.c",
    "noise_reduction_util.c",
    "pcan_gain_control.c",
    "pcan_gain_control_util.c",
    "window.c",
    "window_util.c",
)
CXX_SOURCES = ("fft.cc", "fft_util.cc", "kiss_fft_int16.cc")


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def digest_sources(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(ROOT).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def build(
    output: Path,
    cc: str,
    cxx: str,
    c_flags: list[str],
    cxx_flags: list[str],
) -> dict[str, str]:
    output = output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    sources = [FRONTEND / name for name in C_SOURCES + CXX_SOURCES]
    sources.extend((BRIDGE, KISSFFT / "kiss_fft.c", KISSFFT / "tools/kiss_fftr.c"))
    missing = [str(path) for path in sources if not path.is_file()]
    if missing:
        raise FileNotFoundError(f"missing frontend sources: {', '.join(missing)}")

    common = ["-fPIC", "-O2", "-Wall", "-Wextra", "-Werror"]
    includes = [
        "-I", str(TFLM),
        "-I", str(KISSFFT),
        "-I", str(BRIDGE.parent),
    ]
    with tempfile.TemporaryDirectory(
        prefix=".r528-kws-microfrontend-", dir=output.parent
    ) as temp:
        build_dir = Path(temp)
        objects: list[Path] = []
        for index, name in enumerate(C_SOURCES):
            obj = build_dir / f"c-{index}.o"
            run(
                [cc, "-std=c11", *common, *c_flags, *includes, "-c",
                 str(FRONTEND / name), "-o", str(obj)]
            )
            objects.append(obj)
        for index, source in enumerate(
            [FRONTEND / name for name in CXX_SOURCES] + [BRIDGE]
        ):
            obj = build_dir / f"cxx-{index}.o"
            run(
                [cxx, "-std=c++17", *common, "-Wno-unused-parameter",
                 *cxx_flags, *includes, "-c", str(source), "-o", str(obj)]
            )
            objects.append(obj)

        temporary_output = build_dir / output.name
        run([cc, "-shared", *map(str, objects), "-lm", "-o", str(temporary_output)])
        library = ctypes.CDLL(str(temporary_output))
        library.r528_kws_feature_channels.restype = ctypes.c_size_t
        library.r528_kws_feature_frames.argtypes = [ctypes.c_size_t]
        library.r528_kws_feature_frames.restype = ctypes.c_size_t
        if library.r528_kws_feature_channels() != 40:
            raise RuntimeError("built frontend reports the wrong channel count")
        if library.r528_kws_feature_frames(57_344) != 178:
            raise RuntimeError("built frontend reports the wrong frame count")
        library.r528_kws_align_audio.argtypes = [
            ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        library.r528_kws_align_audio.restype = ctypes.c_int
        silence = (ctypes.c_int16 * 57_344)()
        aligned = (ctypes.c_int16 * 57_344)()
        onset = ctypes.c_size_t()
        if library.r528_kws_align_audio(
            silence, 57_344, aligned, 57_344, ctypes.byref(onset)
        ) != 1:
            raise RuntimeError("built frontend alignment self-test failed")
        library_digest = hashlib.sha256(temporary_output.read_bytes()).hexdigest()
        os.chmod(temporary_output, 0o600)
        os.replace(temporary_output, output)

    return {
        "library": str(output),
        "library_sha256": library_digest,
        "sources_sha256": digest_sources(sources),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cc", default=os.environ.get("CC") or shutil.which("gcc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("g++"))
    parser.add_argument(
        "--c-flags", default=os.environ.get("R528_KWS_CFLAGS", ""),
        help="additional C compiler flags",
    )
    parser.add_argument(
        "--cxx-flags", default=os.environ.get("R528_KWS_CXXFLAGS", ""),
        help="additional C++ compiler flags",
    )
    args = parser.parse_args()
    if not args.cc or not args.cxx:
        parser.error("both gcc and g++ are required (or pass --cc/--cxx)")
    try:
        result = build(
            args.output,
            args.cc,
            args.cxx,
            shlex.split(args.c_flags),
            shlex.split(args.cxx_flags),
        )
    except (OSError, subprocess.CalledProcessError, RuntimeError) as error:
        print(f"microfrontend 构建失败：{error}", file=os.sys.stderr)
        return 1
    for key, value in result.items():
        print(f"{key}={value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
