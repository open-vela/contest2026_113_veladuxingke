#!/usr/bin/env python3
"""Validate and deterministically import R528 DMIC KWS corpus samples."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import stat
import struct
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SPEC = Path(__file__).with_name("kws_corpus_spec.json")
DEFAULT_OUTPUT = Path.home() / "r528-kws-corpus"


@dataclass(frozen=True)
class ValidatedSample:
    sample_id: str
    label: str
    pcm_path: Path
    metadata_path: Path
    pcm: bytes
    metadata: dict
    metrics: dict
    warnings: tuple[str, ...]


def canonical_json(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_spec(path: Path = DEFAULT_SPEC) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema_version") != 1 or not isinstance(value.get("labels"), dict):
        raise ValueError("unsupported corpus specification")
    return value


def ensure_regular_file(path: Path, maximum: int) -> bytes:
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ValueError(f"not a regular file: {path.name}")
    if info.st_size <= 0 or info.st_size > maximum:
        raise ValueError(f"file size outside allowed range: {path.name}")
    return path.read_bytes()


def measure_pcm(pcm: bytes, spec: dict) -> dict:
    if not pcm or len(pcm) % 2:
        raise ValueError("PCM must contain complete signed 16-bit samples")

    sample_count = len(pcm) // 2
    samples = struct.unpack(f"<{sample_count}h", pcm)
    peak = max(abs(sample) for sample in samples)
    sum_squares = sum(sample * sample for sample in samples)
    rms = math.sqrt(sum_squares / sample_count)
    mean = sum(samples) / sample_count
    clipped = sum(sample in (-32768, 32767) for sample in samples)
    activity = spec["activity"]
    frame_samples = activity["frame_samples"]
    active_frames = 0
    frames = 0
    for offset in range(0, sample_count, frame_samples):
        frame = samples[offset : offset + frame_samples]
        energy = sum(sample * sample for sample in frame) / len(frame)
        frame_peak = max(abs(sample) for sample in frame)
        if (
            frame_peak >= activity["minimum_peak"]
            and energy >= activity["minimum_rms"] ** 2
        ):
            active_frames += 1
        frames += 1

    rate = spec["audio"]["sample_rate_hz"]
    return {
        "bytes": len(pcm),
        "samples": sample_count,
        "duration_ms": sample_count * 1000 / rate,
        "peak": peak,
        "rms": round(rms, 6),
        "rms_dbfs": None if rms == 0 else round(20 * math.log10(rms / 32768), 6),
        "dc_mean": round(mean, 6),
        "clipped_samples": clipped,
        "clipped_ratio": round(clipped / sample_count, 9),
        "active_frames": active_frames,
        "analysis_frames": frames,
        "active_ratio": round(active_frames / frames, 9),
    }


def make_wav(pcm: bytes, spec: dict) -> bytes:
    import io

    output = io.BytesIO()
    with wave.open(output, "wb") as target:
        target.setnchannels(spec["audio"]["channels"])
        target.setsampwidth(spec["audio"]["bits_per_sample"] // 8)
        target.setframerate(spec["audio"]["sample_rate_hz"])
        target.writeframes(pcm)
    result = output.getvalue()
    with wave.open(io.BytesIO(result), "rb") as source:
        if source.readframes(source.getnframes()) != pcm:
            raise ValueError("WAV payload differs from source PCM")
    return result


def validate_metadata(metadata: dict, sample_id: str, spec: dict) -> tuple[str, int]:
    required = {
        "schema_version",
        "sample_id",
        "device_id",
        "speaker_id",
        "session_id",
        "label",
        "take",
        "capture_device",
        "encoding",
        "sample_rate_hz",
        "channels",
        "bits_per_sample",
        "requested_ms",
        "actual_bytes",
        "frames",
        "duration_ms",
        "status",
    }
    if not required.issubset(metadata):
        raise ValueError(f"missing metadata fields for {sample_id}")
    if metadata["schema_version"] != 1 or metadata["status"] != "complete":
        raise ValueError(f"incomplete metadata for {sample_id}")

    match = re.fullmatch(spec["id"]["sample_pattern"], sample_id)
    if match is None:
        raise ValueError(f"invalid sample ID: {sample_id}")
    take = int(match.group("take"))
    labels_by_code = {
        code: label for label, code in spec["id"]["label_codes"].items()
    }
    label = labels_by_code[match.group("label_code")]
    expected = {
        "sample_id": sample_id,
        "label": label,
        "take": take,
        "encoding": spec["audio"]["encoding"],
        "sample_rate_hz": spec["audio"]["sample_rate_hz"],
        "channels": spec["audio"]["channels"],
        "bits_per_sample": spec["audio"]["bits_per_sample"],
    }
    for key, value in expected.items():
        if metadata.get(key) != value:
            raise ValueError(f"metadata {key} mismatch for {sample_id}")
    id_pattern = re.compile(spec["id"]["pattern"])
    for key in ("device_id", "speaker_id", "session_id"):
        if not isinstance(metadata.get(key), str) or not id_pattern.fullmatch(metadata[key]):
            raise ValueError(f"metadata {key} is invalid for {sample_id}")
    if label not in spec["labels"] or take < 1:
        raise ValueError(f"unsupported label or take for {sample_id}")
    if not isinstance(metadata["capture_device"], str) or not metadata["capture_device"].startswith("/"):
        raise ValueError(f"invalid capture device for {sample_id}")
    return label, take


def relabel_sample(
    sample_id: str,
    label: str,
    take: int,
    metadata: dict,
    spec: dict,
    session_relabels: dict[str, str],
) -> tuple[str, str, dict]:
    target_label = session_relabels.get(metadata["session_id"])
    if target_label is None:
        return sample_id, label, metadata
    if label != "unknown_speech":
        raise ValueError(
            f"session relabel source must be unknown_speech: {sample_id}"
        )
    if target_label not in spec["labels"] or target_label == "unknown_speech":
        raise ValueError(f"invalid session relabel target: {target_label}")

    match = re.fullmatch(spec["id"]["sample_pattern"], sample_id)
    if match is None:
        raise ValueError(f"invalid sample ID: {sample_id}")
    target_code = spec["id"]["label_codes"].get(target_label)
    if target_code is None:
        raise ValueError(f"missing label code for {target_label}")
    target_id = (
        f"k{match.group('context_hash')}_{target_code}_{take:04d}"
    )
    target_metadata = dict(metadata)
    target_metadata["sample_id"] = target_id
    target_metadata["label"] = target_label
    target_metadata["relabelled_from"] = {
        "sample_id": sample_id,
        "label": label,
    }
    return target_id, target_label, target_metadata


def validate_staging(
    staging: Path,
    spec: dict,
    session_relabels: dict[str, str] | None = None,
) -> list[ValidatedSample]:
    if staging.is_symlink() or not staging.is_dir():
        raise ValueError("staging must be a real directory")

    entries = list(staging.iterdir())
    if any(entry.is_symlink() for entry in entries):
        raise ValueError("staging must not contain symbolic links")
    parts = [entry.name for entry in entries if ".part" in entry.name]
    if parts:
        raise ValueError(f"unfinished .part files found: {', '.join(sorted(parts))}")

    pcms = {entry.stem: entry for entry in entries if entry.suffix == ".pcm"}
    metadata_files = {entry.stem: entry for entry in entries if entry.suffix == ".json"}
    unsupported = [
        entry.name
        for entry in entries
        if entry.is_file() and entry.suffix not in {".pcm", ".json"}
    ]
    if unsupported:
        raise ValueError(f"unsupported staging files: {', '.join(sorted(unsupported))}")
    if not pcms or pcms.keys() != metadata_files.keys():
        missing_pcm = sorted(metadata_files.keys() - pcms.keys())
        missing_json = sorted(pcms.keys() - metadata_files.keys())
        raise ValueError(f"unpaired samples: pcm={missing_pcm}, json={missing_json}")

    maximum = spec["audio"]["maximum_bytes"]
    session_relabels = session_relabels or {}
    validated = []
    validated_ids = set()
    for sample_id in sorted(pcms):
        pcm = ensure_regular_file(pcms[sample_id], maximum)
        metadata_raw = ensure_regular_file(metadata_files[sample_id], 64 * 1024)
        try:
            metadata = json.loads(metadata_raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"invalid JSON for {sample_id}") from error
        if not isinstance(metadata, dict):
            raise ValueError(f"metadata must be an object for {sample_id}")
        label, take = validate_metadata(metadata, sample_id, spec)
        imported_id, label, metadata = relabel_sample(
            sample_id,
            label,
            take,
            metadata,
            spec,
            session_relabels,
        )
        if imported_id in validated_ids:
            raise ValueError(f"duplicate imported sample ID: {imported_id}")
        validated_ids.add(imported_id)
        metrics = measure_pcm(pcm, spec)
        if metadata["actual_bytes"] != len(pcm) or metadata["frames"] != metrics["samples"]:
            raise ValueError(f"PCM size metadata mismatch for {sample_id}")
        if abs(float(metadata["duration_ms"]) - metrics["duration_ms"]) > 1:
            raise ValueError(f"PCM duration metadata mismatch for {sample_id}")
        requested = int(metadata["requested_ms"])
        tolerance = spec["audio"]["duration_tolerance_ms"]
        if requested < 1000 or requested > 10000 or abs(metrics["duration_ms"] - requested) > tolerance:
            raise ValueError(f"PCM duration outside tolerance for {sample_id}")

        warnings = []
        if metrics["peak"] == 0:
            warnings.append("all_zero")
        quality = spec["quality"]
        if metrics["clipped_ratio"] > quality["maximum_clipped_ratio"]:
            warnings.append("excessive_clipping")
        if (
            label.startswith("query_") or label == "wake_nihao_vela"
        ) and metrics["active_ratio"] < quality["minimum_positive_active_ratio"]:
            warnings.append("low_positive_activity")
        validated.append(
            ValidatedSample(
                imported_id,
                label,
                pcms[sample_id],
                metadata_files[sample_id],
                pcm,
                metadata,
                metrics,
                tuple(warnings),
            )
        )
    return validated


def entry_for(sample: ValidatedSample, wav_data: bytes, spec: dict) -> dict:
    return {
        "schema_version": 1,
        "sample_id": sample.sample_id,
        "label": sample.label,
        "training_class": spec["labels"][sample.label]["training_class"],
        "pcm_sha256": sha256(sample.pcm),
        "wav_sha256": sha256(wav_data),
        "metadata_sha256": sha256(canonical_json(sample.metadata)),
        "metrics": sample.metrics,
        "warnings": list(sample.warnings),
    }


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, "wb") as target:
            target.write(data)
            target.flush()
            os.fsync(target.fileno())
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def destination_targets(sample: ValidatedSample, destination: Path, wav_data: bytes) -> dict[Path, bytes]:
    return {
        destination / "raw" / sample.label / f"{sample.sample_id}.pcm": sample.pcm,
        destination / "wav" / sample.label / f"{sample.sample_id}.wav": wav_data,
        destination / "metadata" / sample.label / f"{sample.sample_id}.json": (
            json.dumps(sample.metadata, ensure_ascii=False, indent=2) + "\n"
        ).encode("utf-8"),
    }


def destination_state(targets: dict[Path, bytes], sample_id: str) -> str:
    exists = [path.exists() for path in targets]
    if not any(exists):
        return "new"
    if not all(exists):
        raise ValueError(f"partial existing sample: {sample_id}")
    if any(path.is_symlink() or path.read_bytes() != data for path, data in targets.items()):
        raise ValueError(f"conflicting existing sample: {sample_id}")
    return "existing"


def ensure_destination(targets: dict[Path, bytes]) -> list[Path]:
    created = []
    try:
        for path, data in targets.items():
            write_atomic(path, data)
            created.append(path)
    except BaseException:
        for path in created:
            path.unlink(missing_ok=True)
        raise
    return created


def read_existing_entries(destination: Path) -> dict[str, dict]:
    manifest = destination / "manifest.jsonl"
    if not manifest.exists():
        return {}
    entries = {}
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if line:
            entry = json.loads(line)
            entries[entry["sample_id"]] = entry
    return entries


def import_corpus(
    staging: Path,
    destination: Path,
    spec_path: Path = DEFAULT_SPEC,
    allow_repo_output: bool = False,
    session_relabels: dict[str, str] | None = None,
) -> dict:
    spec = load_spec(spec_path)
    staging = staging.expanduser().resolve()
    destination = destination.expanduser().resolve()
    if not allow_repo_output and (destination == PROJECT_ROOT or PROJECT_ROOT in destination.parents):
        raise ValueError("corpus output inside the repository requires --allow-repo-output")
    samples = validate_staging(staging, spec, session_relabels)

    prepared = [(sample, make_wav(sample.pcm, spec)) for sample in samples]
    entries = read_existing_entries(destination)
    states = {}
    targets_by_id = {}
    for sample, wav_data in prepared:
        new_entry = entry_for(sample, wav_data, spec)
        old_entry = entries.get(sample.sample_id)
        if old_entry is not None and old_entry != new_entry:
            raise ValueError(f"manifest conflict for {sample.sample_id}")
        targets = destination_targets(sample, destination, wav_data)
        state = destination_state(targets, sample.sample_id)
        if (old_entry is None) != (state == "new"):
            raise ValueError(f"manifest/files disagree for {sample.sample_id}")
        states[sample.sample_id] = state
        targets_by_id[sample.sample_id] = targets

    created = []
    imported = 0
    try:
        for sample, wav_data in prepared:
            if states[sample.sample_id] == "new":
                created.extend(ensure_destination(targets_by_id[sample.sample_id]))
                imported += 1
            entries[sample.sample_id] = entry_for(sample, wav_data, spec)
    except BaseException:
        for path in created:
            path.unlink(missing_ok=True)
        raise

    ordered = [entries[key] for key in sorted(entries)]
    manifest = b"".join(canonical_json(entry) + b"\n" for entry in ordered)
    counts: dict[str, int] = {}
    warning_count = 0
    for entry in ordered:
        counts[entry["label"]] = counts.get(entry["label"], 0) + 1
        warning_count += len(entry["warnings"])
    summary = {
        "schema_version": 1,
        "samples": len(ordered),
        "labels": {key: counts[key] for key in sorted(counts)},
        "quality_warnings": warning_count,
        "manifest_sha256": sha256(manifest),
    }
    try:
        write_atomic(destination / "manifest.jsonl", manifest)
        write_atomic(
            destination / "summary.json",
            (json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        )
    except BaseException:
        for path in created:
            path.unlink(missing_ok=True)
        raise
    return {**summary, "imported_this_run": imported}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("staging", type=Path, help="YMODEM receive directory")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--spec", type=Path, default=DEFAULT_SPEC)
    parser.add_argument("--allow-repo-output", action="store_true")
    parser.add_argument(
        "--relabel-session",
        action="append",
        default=[],
        metavar="SESSION=LABEL",
        help="relabel unknown_speech samples from one capture session",
    )
    args = parser.parse_args()
    try:
        session_relabels = {}
        id_pattern = re.compile(load_spec(args.spec)["id"]["pattern"])
        for value in args.relabel_session:
            session, separator, label = value.partition("=")
            if (
                separator != "="
                or not id_pattern.fullmatch(session)
                or not label
                or session in session_relabels
            ):
                raise ValueError(f"invalid --relabel-session value: {value}")
            session_relabels[session] = label
        summary = import_corpus(
            args.staging,
            args.output,
            args.spec,
            args.allow_repo_output,
            session_relabels,
        )
    except (OSError, ValueError) as error:
        print(f"导入失败：{error}", file=os.sys.stderr)
        return 1
    print(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
