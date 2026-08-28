#!/usr/bin/env python3
"""Build a deterministic, curated split manifest for R528 KWS training."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import tempfile
from collections import Counter
from pathlib import Path

SCHEMA_VERSION = 1
DEFAULT_SEED = "r528-kws-v1"
SPLITS = ("train", "validation", "test")


def canonical_json(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_regular(path: Path, maximum: int | None = None) -> bytes:
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ValueError(f"not a regular file: {path}")
    if maximum is not None and info.st_size > maximum:
        raise ValueError(f"file is too large: {path}")
    return path.read_bytes()


def read_jsonl(path: Path, maximum: int) -> list[dict]:
    data = read_regular(path, maximum)
    rows = []
    for line_number, raw_line in enumerate(data.splitlines(), 1):
        if not raw_line.strip():
            continue
        try:
            value = json.loads(raw_line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"invalid JSON at {path}:{line_number}") from error
        if not isinstance(value, dict):
            raise ValueError(f"JSONL row must be an object at {path}:{line_number}")
        rows.append(value)
    return rows


def load_curation(path: Path, sample_ids: set[str]) -> tuple[dict[str, dict], bytes]:
    if not path.exists():
        return {}, b""
    data = read_regular(path, 1024 * 1024)
    decisions: dict[str, dict] = {}
    for line_number, raw_line in enumerate(data.splitlines(), 1):
        if not raw_line.strip():
            continue
        try:
            row = json.loads(raw_line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"invalid curation JSON at line {line_number}") from error
        required = {
            "schema_version",
            "sample_id",
            "decision",
            "reason",
            "review_source",
            "reviewed_on",
        }
        if not isinstance(row, dict) or not required.issubset(row):
            raise ValueError(f"invalid curation record at line {line_number}")
        sample_id = row["sample_id"]
        if row["schema_version"] != SCHEMA_VERSION:
            raise ValueError(f"unsupported curation schema at line {line_number}")
        if row["decision"] not in {"include", "exclude"}:
            raise ValueError(f"invalid curation decision at line {line_number}")
        if not all(isinstance(row[key], str) and row[key] for key in required - {"schema_version"}):
            raise ValueError(f"invalid curation fields at line {line_number}")
        if sample_id not in sample_ids:
            raise ValueError(f"curation references unknown sample: {sample_id}")
        if sample_id in decisions:
            raise ValueError(f"duplicate curation decision: {sample_id}")
        decisions[sample_id] = row
    return decisions, data


def split_counts(sample_count: int) -> tuple[int, int, int]:
    if sample_count < 1:
        return 0, 0, 0
    if sample_count == 1:
        return 1, 0, 0
    if sample_count == 2:
        return 1, 1, 0
    validation = max(1, int(sample_count * 0.15 + 0.5))
    test = max(1, int(sample_count * 0.15 + 0.5))
    train = sample_count - validation - test
    if train < 1:
        test -= 1
        train += 1
    return train, validation, test


def assign_splits(entries: list[dict], seed: str) -> None:
    labels: dict[str, list[dict]] = {}
    for entry in entries:
        labels.setdefault(entry["label"], []).append(entry)
    for label_entries in labels.values():
        ordered = sorted(
            label_entries,
            key=lambda entry: hashlib.sha256(
                f"{seed}\0{entry['sample_id']}".encode("ascii")
            ).digest(),
        )
        train, validation, test = split_counts(len(ordered))
        split_names = (
            ["train"] * train
            + ["validation"] * validation
            + ["test"] * test
        )
        for entry, split in zip(ordered, split_names, strict=True):
            entry["split"] = split


def validate_manifest_entry(corpus: Path, row: dict) -> dict:
    required = {
        "schema_version",
        "sample_id",
        "label",
        "training_class",
        "pcm_sha256",
        "wav_sha256",
        "metadata_sha256",
        "metrics",
        "warnings",
    }
    if not required.issubset(row) or row["schema_version"] != SCHEMA_VERSION:
        raise ValueError("unsupported corpus manifest entry")
    sample_id = row["sample_id"]
    label = row["label"]
    if not isinstance(sample_id, str) or not isinstance(label, str):
        raise ValueError("invalid sample identity in corpus manifest")
    if row["training_class"] not in {
        "target", "unknown", "background", "wake"
    }:
        raise ValueError(f"invalid training class for {sample_id}")
    if not isinstance(row["warnings"], list):
        raise ValueError(f"invalid warnings for {sample_id}")

    wav_relative = Path("wav") / label / f"{sample_id}.wav"
    metadata_relative = Path("metadata") / label / f"{sample_id}.json"
    wav_path = corpus / wav_relative
    metadata_path = corpus / metadata_relative
    wav_data = read_regular(wav_path, 1024 * 1024)
    metadata_data = read_regular(metadata_path, 64 * 1024)
    if sha256(wav_data) != row["wav_sha256"]:
        raise ValueError(f"WAV SHA-256 mismatch for {sample_id}")
    try:
        metadata = json.loads(metadata_data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid metadata for {sample_id}") from error
    if not isinstance(metadata, dict):
        raise ValueError(f"metadata must be an object for {sample_id}")
    if metadata.get("sample_id") != sample_id or metadata.get("label") != label:
        raise ValueError(f"metadata identity mismatch for {sample_id}")
    if sha256(canonical_json(metadata)) != row["metadata_sha256"]:
        raise ValueError(f"metadata SHA-256 mismatch for {sample_id}")
    for key in ("speaker_id", "session_id", "device_id"):
        if not isinstance(metadata.get(key), str) or not metadata[key]:
            raise ValueError(f"missing metadata {key} for {sample_id}")

    return {
        "schema_version": SCHEMA_VERSION,
        "sample_id": sample_id,
        "label": label,
        "training_class": row["training_class"],
        "wav": wav_relative.as_posix(),
        "wav_sha256": row["wav_sha256"],
        "speaker_id": metadata["speaker_id"],
        "session_id": metadata["session_id"],
        "device_id": metadata["device_id"],
    }


def counted(entries: list[dict], key: str) -> dict[str, int]:
    values = Counter(entry[key] for entry in entries)
    return {name: values[name] for name in sorted(values)}


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as target:
            target.write(data)
            target.flush()
            os.fsync(target.fileno())
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def prepare_training_data(
    corpus: Path,
    output: Path | None = None,
    seed: str = DEFAULT_SEED,
) -> dict:
    corpus = corpus.expanduser().resolve()
    if corpus.is_symlink() or not corpus.is_dir():
        raise ValueError("corpus must be a real directory")
    output = (output or corpus / "training").expanduser().resolve()
    if output == corpus or corpus not in output.parents:
        raise ValueError("training output must be a subdirectory of the corpus")
    if not seed or not seed.isascii():
        raise ValueError("split seed must be non-empty ASCII")

    manifest_path = corpus / "manifest.jsonl"
    manifest_data = read_regular(manifest_path, 16 * 1024 * 1024)
    rows = read_jsonl(manifest_path, 16 * 1024 * 1024)
    if not rows:
        raise ValueError("corpus manifest is empty")
    sample_ids = [row.get("sample_id") for row in rows]
    if any(not isinstance(sample_id, str) for sample_id in sample_ids):
        raise ValueError("invalid sample ID in corpus manifest")
    if len(sample_ids) != len(set(sample_ids)):
        raise ValueError("duplicate sample ID in corpus manifest")

    decisions, curation_data = load_curation(
        corpus / "curation.jsonl", set(sample_ids)
    )
    selected = []
    excluded_by_decision = []
    excluded_by_warning = []
    for row in rows:
        sample_id = row["sample_id"]
        entry = validate_manifest_entry(corpus, row)
        decision = decisions.get(sample_id)
        if decision is not None and decision["decision"] == "exclude":
            excluded_by_decision.append(sample_id)
            continue
        if row["warnings"]:
            excluded_by_warning.append(sample_id)
            continue
        selected.append(entry)

    if not selected:
        raise ValueError("no training samples remain after curation")
    assign_splits(selected, seed)
    selected.sort(key=lambda entry: entry["sample_id"])
    selection_data = b"".join(canonical_json(entry) + b"\n" for entry in selected)

    split_counts_summary = {
        split: sum(entry["split"] == split for entry in selected)
        for split in SPLITS
    }
    summary = {
        "schema_version": SCHEMA_VERSION,
        "split_seed": seed,
        "source_manifest_sha256": sha256(manifest_data),
        "curation_sha256": sha256(curation_data),
        "selection_sha256": sha256(selection_data),
        "source_samples": len(rows),
        "selected_samples": len(selected),
        "excluded_by_decision": sorted(excluded_by_decision),
        "excluded_by_warning": sorted(excluded_by_warning),
        "labels": counted(selected, "label"),
        "training_classes": counted(selected, "training_class"),
        "splits": split_counts_summary,
        "speakers": counted(selected, "speaker_id"),
        "sessions": counted(selected, "session_id"),
    }
    write_atomic(output / "selection.jsonl", selection_data)
    write_atomic(
        output / "summary.json",
        (json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode(
            "utf-8"
        ),
    )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--seed", default=DEFAULT_SEED)
    args = parser.parse_args()
    try:
        summary = prepare_training_data(args.corpus, args.output, args.seed)
    except (OSError, ValueError) as error:
        print(f"训练清单生成失败：{error}", file=os.sys.stderr)
        return 1
    print(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
