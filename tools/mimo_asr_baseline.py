#!/usr/bin/env python3
"""Run an opt-in Xiaomi MiMo V2.5 ASR baseline over selected corpus WAVs."""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import os
import stat
import time
import unicodedata
import urllib.error
import urllib.request
from pathlib import Path
from typing import Callable

from import_kws_corpus import canonical_json, load_spec, sha256, write_atomic

PAY_AS_YOU_GO_ENDPOINT = "https://api.xiaomimimo.com/v1/chat/completions"
TOKEN_PLAN_ENDPOINT = "https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
MODEL = "mimo-v2.5-asr"
RETRYABLE_STATUS = {429, 500, 503}
MAX_RESPONSE_BYTES = 2 * 1024 * 1024
MAX_WAV_BYTES = 10 * 1024 * 1024


class SafeRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise RuntimeError(f"redirect rejected (HTTP {code})")


def endpoint_for_key(key: str) -> tuple[str, str]:
    if key.startswith("tp-"):
        return TOKEN_PLAN_ENDPOINT, "token-plan"
    return PAY_AS_YOU_GO_ENDPOINT, "pay-as-you-go"


def read_key_file(path: Path) -> str:
    try:
        info = path.lstat()
    except FileNotFoundError as error:
        raise ValueError(f"API key file does not exist: {path}") from error
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ValueError("API key path must be a regular file")
    if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077:
        raise ValueError("API key file must be current-user-owned mode 0600")
    if info.st_size <= 0 or info.st_size > 64 * 1024:
        raise ValueError("API key file size is invalid")
    key = path.read_text(encoding="utf-8").strip()
    if not key or "\n" in key or "\r" in key:
        raise ValueError("API key must contain exactly one non-empty line")
    return key


def read_limited(response, maximum: int) -> bytes:
    data = response.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError("API response is too large")
    return data


def open_request(request: urllib.request.Request, timeout: float):
    return urllib.request.build_opener(SafeRedirectHandler()).open(
        request, timeout=timeout
    )


def request_body(wav_data: bytes) -> dict:
    if not wav_data or len(wav_data) > MAX_WAV_BYTES:
        raise ValueError("WAV size is outside MiMo ASR limits")
    if wav_data[:4] != b"RIFF" or wav_data[8:12] != b"WAVE":
        raise ValueError("input is not a RIFF/WAVE file")
    encoded = base64.b64encode(wav_data).decode("ascii")
    return {
        "model": MODEL,
        "messages": [
            {
                "role": "user",
                "content": [
                    {
                        "type": "input_audio",
                        "input_audio": {
                            "data": f"data:audio/wav;base64,{encoded}"
                        },
                    }
                ],
            }
        ],
        "asr_options": {"language": "zh"},
        "stream": False,
    }


def fetch_transcript(
    key: str,
    body: dict,
    timeout: float,
    attempts: int,
    open_fn: Callable = open_request,
    sleep_fn: Callable[[float], None] = time.sleep,
    endpoint: str | None = None,
) -> tuple[str, dict]:
    endpoint = endpoint or endpoint_for_key(key)[0]
    payload = canonical_json(body)
    for attempt in range(1, attempts + 1):
        request = urllib.request.Request(
            endpoint,
            data=payload,
            headers={
                "api-key": key,
                "Content-Type": "application/json",
                "Accept": "application/json",
                "User-Agent": "r528-kws-mimo-baseline/1",
            },
            method="POST",
        )
        try:
            with open_fn(request, timeout) as response:
                status = getattr(response, "status", 200)
                if status != 200:
                    raise urllib.error.HTTPError(
                        endpoint, status, "unexpected status", response.headers, None
                    )
                raw = read_limited(response, MAX_RESPONSE_BYTES)
            break
        except urllib.error.HTTPError as error:
            if error.code in RETRYABLE_STATUS and attempt < attempts:
                sleep_fn(float(2 ** (attempt - 1)))
                continue
            raise RuntimeError(f"API returned HTTP {error.code}") from None
        except urllib.error.URLError as error:
            raise RuntimeError(
                f"API connection failed ({type(error.reason).__name__})"
            ) from None
    else:
        raise RuntimeError("API request failed")

    try:
        result = json.loads(raw.decode("utf-8"))
        choice = result["choices"][0]
        transcript = choice["message"]["content"]
        if not isinstance(transcript, str) or not transcript.strip():
            raise ValueError
    except (UnicodeDecodeError, json.JSONDecodeError, KeyError, IndexError, TypeError, ValueError):
        raise ValueError("API response has no valid choices[0].message.content") from None
    return transcript, {
        "response_id": result.get("id"),
        "response_model": result.get("model"),
        "finish_reason": choice.get("finish_reason"),
    }


def normalize_transcript(text: str) -> str:
    normalized = unicodedata.normalize("NFKC", text).lower()
    return "".join(
        character
        for character in normalized
        if not (
            character.isspace()
            or unicodedata.category(character).startswith("P")
            or unicodedata.category(character).startswith("Z")
        )
    )


def phrase_index(spec: dict) -> dict[str, str]:
    result = {}
    for label, details in spec["labels"].items():
        for phrase in details["phrases"]:
            normalized = normalize_transcript(phrase)
            previous = result.get(normalized)
            if previous is not None and previous != label:
                raise ValueError(f"ambiguous normalized phrase: {phrase}")
            result[normalized] = label
    return result


def load_manifest(corpus: Path) -> dict[str, dict]:
    result = {}
    manifest = corpus / "manifest.jsonl"
    if not manifest.is_file() or manifest.is_symlink():
        raise ValueError("corpus manifest.jsonl is missing")
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if line:
            entry = json.loads(line)
            result[entry["sample_id"]] = entry
    return result


def load_existing_records(corpus: Path, manifest: dict[str, dict],
                          phrases: dict[str, str]) -> dict[str, dict]:
    path = corpus / "baseline" / f"{MODEL}.jsonl"
    if not path.exists():
        return {}
    if path.is_symlink() or not path.is_file():
        raise ValueError("baseline result must be a regular file")

    records = {}
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), 1
    ):
        if not line:
            continue
        try:
            record = json.loads(line)
            sample_id = record["sample_id"]
            entry = manifest[sample_id]
            transcript = record["transcript"]
        except (json.JSONDecodeError, KeyError, TypeError) as error:
            raise ValueError(
                f"invalid existing baseline record at line {line_number}"
            ) from error
        if (
            not isinstance(record, dict)
            or not isinstance(sample_id, str)
            or not isinstance(transcript, str)
            or not transcript.strip()
            or record.get("model") != MODEL
            or record.get("human_label") != entry["label"]
            or sample_id in records
        ):
            raise ValueError(
                f"invalid existing baseline record at line {line_number}"
            )

        normalized = normalize_transcript(transcript)
        predicted = phrases.get(normalized, "unknown_speech")
        record["normalized_transcript"] = normalized
        record["predicted_label"] = predicted
        record["needs_review"] = predicted != entry["label"]
        records[sample_id] = record
    return records


def write_baseline_records(corpus: Path, records: dict[str, dict],
                           account_type: str) -> dict:
    ordered = [records[sample_id] for sample_id in sorted(records)]
    output = corpus / "baseline"
    data = b"".join(canonical_json(record) + b"\n" for record in ordered)
    summary = {
        "schema_version": 1,
        "model": MODEL,
        "account_type": account_type,
        "samples": len(ordered),
        "needs_review": sum(record["needs_review"] for record in ordered),
        "result_sha256": sha256(data),
    }
    write_atomic(output / f"{MODEL}.jsonl", data)
    write_atomic(
        output / "summary.json",
        (json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode(),
    )
    return summary


def run_baseline(
    key_file: Path,
    corpus: Path,
    sample_ids: list[str],
    timeout: float = 90,
    attempts: int = 3,
    fetch_fn: Callable = fetch_transcript,
) -> dict:
    if not sample_ids or len(set(sample_ids)) != len(sample_ids):
        raise ValueError("select one or more unique sample IDs")
    spec = load_spec()
    phrases = phrase_index(spec)
    manifest = load_manifest(corpus)
    key = read_key_file(key_file)
    endpoint, account_type = endpoint_for_key(key)
    records = load_existing_records(corpus, manifest, phrases)
    processed = 0
    skipped = 0
    try:
        for index, sample_id in enumerate(sample_ids, 1):
            entry = manifest.get(sample_id)
            if entry is None:
                raise ValueError(f"sample not found in manifest: {sample_id}")
            if sample_id in records:
                skipped += 1
                continue
            wav_path = corpus / "wav" / entry["label"] / f"{sample_id}.wav"
            if wav_path.is_symlink() or not wav_path.is_file():
                raise ValueError(f"selected WAV is missing: {sample_id}")
            wav_data = wav_path.read_bytes()
            if sha256(wav_data) != entry["wav_sha256"]:
                raise ValueError(f"selected WAV hash mismatch: {sample_id}")
            transcript, response = fetch_fn(
                key,
                request_body(wav_data),
                timeout,
                attempts,
                endpoint=endpoint,
            )
            normalized = normalize_transcript(transcript)
            predicted = phrases.get(normalized, "unknown_speech")
            records[sample_id] = {
                "schema_version": 1,
                "sample_id": sample_id,
                "human_label": entry["label"],
                "transcript": transcript,
                "normalized_transcript": normalized,
                "predicted_label": predicted,
                "needs_review": predicted != entry["label"],
                "model": MODEL,
                **response,
            }
            processed += 1
            write_baseline_records(corpus, records, account_type)
            print(f"MiMo ASR: {index}/{len(sample_ids)} {sample_id}", flush=True)
    finally:
        key = ""

    summary = write_baseline_records(corpus, records, account_type)
    return {
        **summary,
        "selected": len(sample_ids),
        "processed_this_run": processed,
        "skipped_existing": skipped,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--key-file", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--sample", action="append", required=True)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--attempts", type=int, default=3)
    args = parser.parse_args()
    if args.timeout <= 0 or args.attempts < 1 or args.attempts > 5:
        parser.error("invalid timeout or attempts")
    try:
        summary = run_baseline(
            args.key_file, args.corpus, args.sample, args.timeout, args.attempts
        )
    except (OSError, ValueError, RuntimeError, binascii.Error) as error:
        print(f"MiMo ASR 基线失败：{error}", file=os.sys.stderr)
        return 1
    print(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
