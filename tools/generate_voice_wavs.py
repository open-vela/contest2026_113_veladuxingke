#!/usr/bin/env python3
"""Generate fixed Mandarin prompt WAV files with sherpa-onnx VITS."""

import argparse
from pathlib import Path

import sherpa_onnx

PROMPTS = {
    "current_temperature_is": "当前温度是",
    "below_zero": "零下",
    "zero": "零",
    "one": "一",
    "two": "二",
    "three": "三",
    "four": "四",
    "five": "五",
    "six": "六",
    "seven": "七",
    "eight": "八",
    "nine": "九",
    "ten": "十",
    "hundred": "百",
    "point": "点",
    "degree_celsius": "摄氏度",
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--tokens", type=Path, required=True)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--speaker", type=int, default=0)
    parser.add_argument("--speed", type=float, default=1.0)
    args = parser.parse_args()

    for path in (args.model, args.tokens, args.lexicon):
        if not path.is_file():
            raise FileNotFoundError(path)

    vits = sherpa_onnx.OfflineTtsVitsModelConfig()
    vits.model = str(args.model)
    vits.tokens = str(args.tokens)
    vits.lexicon = str(args.lexicon)

    model = sherpa_onnx.OfflineTtsModelConfig()
    model.vits = vits
    model.num_threads = 2

    config = sherpa_onnx.OfflineTtsConfig()
    config.model = model
    if not config.validate():
        raise RuntimeError("invalid sherpa-onnx TTS configuration")

    tts = sherpa_onnx.OfflineTts(config)
    if args.speaker < 0 or args.speaker >= tts.num_speakers:
        raise ValueError(f"speaker must be in 0..{tts.num_speakers - 1}")

    args.output.mkdir(parents=True, exist_ok=True)
    for stem, text in PROMPTS.items():
        audio = tts.generate(text, sid=args.speaker, speed=args.speed)
        target = args.output / f"{stem}.wav"
        if not sherpa_onnx.write_wave(
            str(target), audio.samples, audio.sample_rate
        ):
            raise RuntimeError(f"failed to write {target}")
        print(f"{stem}: {text} ({len(audio.samples)} samples)")


if __name__ == "__main__":
    main()
