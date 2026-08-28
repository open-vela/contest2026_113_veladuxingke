#!/usr/bin/env python3
"""ctypes wrapper for the R528 fixed-point KWS frontend bridge."""

from __future__ import annotations

import ctypes
import stat
import wave
from pathlib import Path

import numpy as np

SAMPLE_RATE = 16_000
WINDOW_SAMPLES = 480
STRIDE_SAMPLES = 320
FEATURE_CHANNELS = 40


class Microfrontend:
    """Extract bit-stable INT8 Micro Speech features from PCM16 samples."""

    def __init__(self, library: Path):
        library = library.expanduser().resolve()
        info = library.lstat()
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
            raise ValueError(f"frontend library is not a regular file: {library}")
        self.library_path = library
        self._library = ctypes.CDLL(str(library))
        self._library.r528_kws_feature_channels.argtypes = []
        self._library.r528_kws_feature_channels.restype = ctypes.c_size_t
        self._library.r528_kws_feature_frames.argtypes = [ctypes.c_size_t]
        self._library.r528_kws_feature_frames.restype = ctypes.c_size_t
        self._library.r528_kws_extract_features.argtypes = [
            ctypes.POINTER(ctypes.c_int16),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_int8),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self._library.r528_kws_extract_features.restype = ctypes.c_int
        self._library.r528_kws_align_audio.argtypes = [
            ctypes.POINTER(ctypes.c_int16),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_int16),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self._library.r528_kws_align_audio.restype = ctypes.c_int
        if self._library.r528_kws_feature_channels() != FEATURE_CHANNELS:
            raise ValueError("frontend library has an incompatible channel count")

    def feature_frames(self, sample_count: int) -> int:
        if sample_count < 0:
            raise ValueError("sample count cannot be negative")
        return int(self._library.r528_kws_feature_frames(sample_count))

    def extract(self, samples: np.ndarray) -> np.ndarray:
        samples = np.asarray(samples)
        if samples.ndim != 1 or samples.dtype != np.int16:
            raise ValueError("samples must be a one-dimensional int16 array")
        samples = np.ascontiguousarray(samples)
        frames = self.feature_frames(samples.size)
        if frames == 0:
            raise ValueError("audio is shorter than the 30 ms feature window")
        output = np.empty((frames, FEATURE_CHANNELS), dtype=np.int8)
        produced = ctypes.c_size_t()
        status = self._library.r528_kws_extract_features(
            samples.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
            samples.size,
            output.ctypes.data_as(ctypes.POINTER(ctypes.c_int8)),
            output.size,
            ctypes.byref(produced),
        )
        if status != 0 or produced.value != frames:
            raise RuntimeError(
                f"microfrontend failed: status={status}, "
                f"frames={produced.value}/{frames}"
            )
        return output

    def align(self, samples: np.ndarray) -> tuple[np.ndarray, int | None]:
        samples = np.asarray(samples)
        if samples.ndim != 1 or samples.dtype != np.int16:
            raise ValueError("samples must be a one-dimensional int16 array")
        samples = np.ascontiguousarray(samples)
        output = np.empty_like(samples)
        onset = ctypes.c_size_t()
        status = self._library.r528_kws_align_audio(
            samples.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
            samples.size,
            output.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
            output.size,
            ctypes.byref(onset),
        )
        if status < 0:
            raise RuntimeError(f"audio alignment failed: status={status}")
        return output, None if status > 0 else int(onset.value)

    def extract_aligned(self, samples: np.ndarray) -> tuple[np.ndarray, int | None]:
        aligned, onset = self.align(samples)
        return self.extract(aligned), onset

    def read_wav(self, path: Path) -> np.ndarray:
        path = path.expanduser().resolve()
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
            raise ValueError(f"WAV is not a regular file: {path}")
        with wave.open(str(path), "rb") as wav_file:
            properties = (
                wav_file.getframerate(),
                wav_file.getnchannels(),
                wav_file.getsampwidth(),
                wav_file.getcomptype(),
            )
            if properties != (SAMPLE_RATE, 1, 2, "NONE"):
                raise ValueError(f"unsupported WAV format for {path.name}: {properties}")
            frame_count = wav_file.getnframes()
            payload = wav_file.readframes(frame_count)
            if len(payload) != frame_count * 2:
                raise ValueError(f"truncated WAV payload: {path.name}")
        return np.frombuffer(payload, dtype="<i2").copy()

    def extract_wav(self, path: Path) -> np.ndarray:
        return self.extract(self.read_wav(path))
