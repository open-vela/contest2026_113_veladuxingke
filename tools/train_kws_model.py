#!/usr/bin/env python3
"""Train, fully quantize, and gate the personal R528 wake/query KWS model."""

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
MODEL_LABELS = (
    "background",
    "unknown_speech",
    "query_humidity",
    "query_light",
    "query_air_quality",
    "query_temperature",
    "wake_nihao_vela",
)
LABELS = MODEL_LABELS
TRAINING_CLASSES = ("background", "unknown", "target", "wake")
INTENT_LABELS = MODEL_LABELS[2:]
ALLOWED_SPLITS = ("train", "validation", "test")
DEFAULT_SEED = 2_026_082_6
MAX_MODEL_BYTES = 256 * 1024
MODEL_ARCHITECTURE = "conv3_temporal_grid_sensor_aux_v1"
TIMING_STRESS_ONSET_FRAMES = (0, 8, 20, 30, 40, 50)
TRAINING_TIME_SHIFTS = (-40, 0)
TEMPORAL_WARP_FACTORS = (0.9, 1.1)
FRONTEND_STRIDE_SAMPLES = 320
ALIGNMENT_ANCHOR_FRAME = 30
QUIET_FILL_START = 3200
QUIET_FILL_END = 6400


def canonical_json(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def model_label(row: dict) -> str:
    label = row["label"]
    if label in MODEL_LABELS:
        return label
    training_class = row["training_class"]
    if training_class == "background":
        return "background"
    if training_class == "target":
        return "query_temperature"
    return "unknown_speech"


def evaluation_label(model_label_name: str) -> str:
    return model_label_name


def read_regular(path: Path, maximum: int) -> bytes:
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ValueError(f"not a regular file: {path}")
    if info.st_size > maximum:
        raise ValueError(f"file is too large: {path}")
    return path.read_bytes()


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


def choose_validation_thresholds(predictions: list[dict]) -> dict[str, int]:
    thresholds = {}
    for label in INTENT_LABELS:
        negatives = [
            row["scores_raw"][label]
            for row in predictions
            if row["actual"] != label and row["predicted"] == label
        ]
        positives = [
            row["scores_raw"][label]
            for row in predictions
            if row["actual"] == label
        ]
        has_negative = any(row["actual"] != label for row in predictions)
        if not has_negative or not positives:
            raise ValueError(
                f"validation requires positive and negative samples for {label}"
            )
        thresholds[label] = min(max(negatives, default=-128) + 1, 128)
    return thresholds


def threshold_metrics(
    predictions: list[dict], thresholds_raw: dict[str, int]
) -> dict:
    per_label = {}
    total_positives = 0
    total_true_accepts = 0
    total_false_accepts = 0
    for label in INTENT_LABELS:
        positives = [row for row in predictions if row["actual"] == label]
        negatives = [row for row in predictions if row["actual"] != label]
        threshold = thresholds_raw[label]
        true_accepts = sum(
            row["predicted"] == label
            and row["scores_raw"][label] >= threshold
            for row in positives
        )
        false_accepts = sum(
            row["predicted"] == label
            and row["scores_raw"][label] >= threshold
            for row in negatives
        )
        per_label[label] = {
            "positive_samples": len(positives),
            "negative_samples": len(negatives),
            "true_accepts": true_accepts,
            "false_accepts": false_accepts,
            "recall": true_accepts / len(positives),
        }
        total_positives += len(positives)
        total_true_accepts += true_accepts
        total_false_accepts += false_accepts
    return {
        "intent_samples": total_positives,
        "true_accepts": total_true_accepts,
        "false_accepts": total_false_accepts,
        "intent_recall": total_true_accepts / total_positives,
        "per_label": per_label,
    }


def classification_metrics(predictions: list[dict]) -> dict:
    confusion = {
        actual: {predicted: 0 for predicted in LABELS} for actual in LABELS
    }
    correct = 0
    for row in predictions:
        confusion[row["actual"]][row["predicted"]] += 1
        correct += row["actual"] == row["predicted"]
    return {
        "samples": len(predictions),
        "correct": correct,
        "accuracy": correct / len(predictions),
        "confusion": confusion,
    }


def gate_report(report: dict) -> tuple[bool, list[str]]:
    reasons = []
    quantization = report["quantization"]
    validation = report["evaluation"]["validation"]
    test = report["evaluation"]["test"]
    timing_validation = report["timing_stress"]["validation"]
    timing_test = report["timing_stress"]["test"]
    agreement = report["quantization_agreement"]
    if not quantization["full_int8"]:
        reasons.append("model is not full INT8")
    if not quantization["frontend_input_identity"]:
        reasons.append("model input quantization is not scale=1, zero_point=0")
    if report["model"]["bytes"] > MAX_MODEL_BYTES:
        reasons.append("model exceeds the 256 KiB size gate")
    if agreement["argmax_agreement"] < 0.95:
        reasons.append("float/INT8 argmax agreement is below 95%")
    if agreement["maximum_probability_delta"] > 0.08:
        reasons.append("float/INT8 probability delta exceeds 0.08")
    for name, metrics in (
        ("validation", validation),
        ("test", test),
        ("timing validation", timing_validation),
        ("timing test", timing_test),
    ):
        if metrics["threshold"]["false_accepts"] != 0:
            reasons.append(f"{name} has an intent false accept")
        if metrics["threshold"]["intent_recall"] < 0.85:
            reasons.append(f"{name} intent recall is below 85%")
        if metrics["classification"]["accuracy"] < 0.75:
            reasons.append(f"{name} seven-class accuracy is below 75%")
    return not reasons, reasons


def load_selection(corpus: Path) -> tuple[list[dict], dict, bytes]:
    selection_path = corpus / "training/selection.jsonl"
    summary_path = corpus / "training/summary.json"
    selection_data = read_regular(selection_path, 16 * 1024 * 1024)
    summary_data = read_regular(summary_path, 1024 * 1024)
    try:
        summary = json.loads(summary_data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("invalid training summary") from error
    if not isinstance(summary, dict) or summary.get("schema_version") != 1:
        raise ValueError("unsupported training summary")
    if summary.get("selection_sha256") != sha256(selection_data):
        raise ValueError("training selection SHA-256 does not match its summary")

    rows = []
    identities = set()
    required = {
        "schema_version",
        "sample_id",
        "label",
        "training_class",
        "wav",
        "wav_sha256",
        "speaker_id",
        "session_id",
        "device_id",
        "split",
    }
    for line_number, line in enumerate(selection_data.splitlines(), 1):
        try:
            row = json.loads(line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"invalid selection JSON at line {line_number}") from error
        if (
            not isinstance(row, dict)
            or not required.issubset(row)
            or row["schema_version"] != SCHEMA_VERSION
            or row["training_class"] not in TRAINING_CLASSES
            or row["split"] not in ALLOWED_SPLITS
        ):
            raise ValueError(f"invalid selection row at line {line_number}")
        sample_id = row["sample_id"]
        if not isinstance(sample_id, str) or sample_id in identities:
            raise ValueError(f"duplicate or invalid sample ID at line {line_number}")
        wav = Path(row["wav"])
        if wav.is_absolute() or ".." in wav.parts:
            raise ValueError(f"unsafe WAV path for {sample_id}")
        identities.add(sample_id)
        rows.append(row)
    if not rows or len(rows) != summary.get("selected_samples"):
        raise ValueError("selection count does not match its summary")
    for split in ALLOWED_SPLITS:
        classes = {model_label(row) for row in rows if row["split"] == split}
        if classes != set(MODEL_LABELS):
            raise ValueError(f"split {split} does not contain all model labels")
    return rows, summary, selection_data


def time_shift_samples(samples, shift: int, np):
    sample_shift = shift * FRONTEND_STRIDE_SAMPLES
    if abs(sample_shift) >= samples.shape[0]:
        raise ValueError("audio shift must leave at least one source sample")
    if shift == 0:
        return samples.copy()

    quiet = samples[QUIET_FILL_START:QUIET_FILL_END]
    if quiet.size != QUIET_FILL_END - QUIET_FILL_START:
        raise ValueError("audio is too short for the quiet fill window")
    result = np.resize(quiet, samples.shape).astype(np.int16)
    if sample_shift > 0:
        result[sample_shift:] = samples[:-sample_shift]
    else:
        result[:sample_shift] = samples[-sample_shift:]
    return result


def quiet_background_feature(feature, np):
    window = 24
    energy = feature.astype(np.int16).mean(axis=1)
    running = np.convolve(energy, np.ones(window, dtype=np.float32), mode="valid")
    start = int(np.argmin(running))
    quiet = feature[start : start + window]
    copies = (feature.shape[0] + window - 1) // window
    return np.concatenate([quiet] * copies, axis=0)[: feature.shape[0]].copy()


def add_feature_noise(feature, rng, np):
    result = feature.astype(np.int16).copy()
    result += int(rng.integers(-3, 4))
    result += rng.integers(-2, 3, size=result.shape, dtype=np.int16)
    return np.clip(result, -128, 127).astype(np.int8)


def warp_feature_time(feature, factor: float, np):
    if feature.ndim != 2 or factor <= 0.0:
        raise ValueError("feature warp requires a 2-D feature and positive factor")
    destination = np.arange(feature.shape[0], dtype=np.float64)
    source = ALIGNMENT_ANCHOR_FRAME + (
        destination - ALIGNMENT_ANCHOR_FRAME
    ) * factor
    warped = np.empty(feature.shape, dtype=np.float64)
    for channel in range(feature.shape[1]):
        warped[:, channel] = np.interp(
            source,
            destination,
            feature[:, channel].astype(np.float64),
            left=-128.0,
            right=-128.0,
        )
    return np.clip(np.rint(warped), -128, 127).astype(np.int8)


def balanced_sample_weights(labels, np):
    labels = np.asarray(labels, dtype=np.int64)
    counts = np.bincount(labels, minlength=len(MODEL_LABELS))
    if labels.ndim != 1 or labels.size == 0 or np.any(counts == 0):
        raise ValueError("sample weights require every training class")
    class_weights = labels.size / (len(MODEL_LABELS) * counts.astype(np.float64))
    return class_weights[labels].astype(np.float32)


def validation_selection_metrics(labels, probabilities, np):
    labels = np.asarray(labels, dtype=np.int64)
    probabilities = np.asarray(probabilities)
    if (
        labels.ndim != 1
        or probabilities.shape != (labels.size, len(MODEL_LABELS))
    ):
        raise ValueError("invalid validation prediction shape")
    predicted = np.argmax(probabilities, axis=1)
    accepted_intents = 0
    intent_samples = 0
    intent_argmax = 0
    intent_false_argmax = 0
    margins = []
    for label in INTENT_LABELS:
        index = MODEL_LABELS.index(label)
        scores = probabilities[:, index]
        positives = labels == index
        negatives = ~positives
        if not np.any(positives) or not np.any(negatives):
            raise ValueError(
                f"validation selection requires positives and negatives for {label}"
            )
        false_argmax = negatives & (predicted == index)
        maximum_negative = (
            float(np.max(scores[false_argmax]))
            if np.any(false_argmax)
            else -1.0
        )
        accepted_intents += int(
            np.sum(positives & (predicted == index) & (scores > maximum_negative))
        )
        intent_samples += int(np.sum(positives))
        intent_argmax += int(np.sum(positives & (predicted == index)))
        intent_false_argmax += int(np.sum(negatives & (predicted == index)))
        margins.append(float(np.min(scores[positives]) - maximum_negative))
    return {
        "accepted_intents": accepted_intents,
        "intent_samples": intent_samples,
        "intent_argmax": intent_argmax,
        "intent_false_argmax": intent_false_argmax,
        "fine_accuracy": float(np.mean(labels == predicted)),
        "minimum_intent_margin": min(margins),
    }


def validation_selection_key(metrics: dict, validation_loss: float) -> tuple:
    return (
        metrics["accepted_intents"],
        metrics["intent_argmax"],
        -metrics["intent_false_argmax"],
        metrics["fine_accuracy"],
        metrics["minimum_intent_margin"],
        -validation_loss,
    )


def timing_stress_set(samples, rows, frontend, np):
    shifted_features = []
    shifted_rows = []
    for sample, row in zip(samples, rows, strict=True):
        _, original_onset = frontend.align(sample)
        for desired_onset in TIMING_STRESS_ONSET_FRAMES:
            if original_onset is None:
                shifted = sample.copy()
            else:
                shift = desired_onset - original_onset // FRONTEND_STRIDE_SAMPLES
                shifted = time_shift_samples(sample, shift, np)
            shifted_features.append(frontend.extract_aligned(shifted)[0])
            shifted_row = row.copy()
            shifted_row["sample_id"] = (
                f"{row['sample_id']}@onset{desired_onset * 20}ms"
            )
            shifted_row["timing_onset_frame"] = desired_onset
            shifted_rows.append(shifted_row)
    return np.stack(shifted_features), shifted_rows


def balanced_training_set(
    train_samples, train_features, train_labels, frontend, seed: int, np
):
    rng = np.random.default_rng(seed)
    variants_by_class = {index: [] for index in range(len(MODEL_LABELS))}
    quiet_background_sources = 0
    for sample, feature, label in zip(
        train_samples, train_features, train_labels, strict=True
    ):
        for shift in TRAINING_TIME_SHIFTS:
            shifted = time_shift_samples(sample, shift, np)
            variants_by_class[int(label)].append(
                frontend.extract_aligned(shifted)[0]
            )
        for factor in TEMPORAL_WARP_FACTORS:
            variants_by_class[int(label)].append(
                warp_feature_time(feature, factor, np)
            )
        if label != MODEL_LABELS.index("background"):
            variants_by_class[MODEL_LABELS.index("background")].append(
                quiet_background_feature(feature, np)
            )
            quiet_background_sources += 1
    original_counts = Counter(int(label) for label in train_labels)
    per_class = max(len(variants) for variants in variants_by_class.values())
    augmented_features = []
    augmented_labels = []
    for label in range(len(MODEL_LABELS)):
        variants = variants_by_class[label]
        for index in range(per_class):
            source = variants[index % len(variants)]
            if index < len(variants):
                feature = source
            else:
                feature = add_feature_noise(source, rng, np)
            augmented_features.append(feature)
            augmented_labels.append(label)
    order = rng.permutation(len(augmented_labels))
    features = np.stack(augmented_features)[order]
    labels = np.asarray(augmented_labels, dtype=np.int64)[order]
    details = {
        "original_class_counts": {
            MODEL_LABELS[index]: original_counts[index]
            for index in range(len(MODEL_LABELS))
        },
        "quiet_background_sources": quiet_background_sources,
        "timing_shift_frames": list(TRAINING_TIME_SHIFTS),
        "timing_shift_fill": "pcm-quiet-window-200ms-to-400ms",
        "temporal_warp_factors": list(TEMPORAL_WARP_FACTORS),
        "temporal_warp_anchor_frame": ALIGNMENT_ANCHOR_FRAME,
        "augmented_samples_per_class": per_class,
        "augmented_samples": int(labels.size),
    }
    return features[..., np.newaxis], labels, details


def build_model(input_shape, tf):
    regularizer = tf.keras.regularizers.l2(1e-4)
    inputs = tf.keras.Input(shape=input_shape, name="micro_features")
    x = tf.keras.layers.Conv2D(
        8, (5, 5), strides=(2, 2), padding="same", use_bias=False,
        kernel_regularizer=regularizer, name="conv_1",
    )(inputs)
    x = tf.keras.layers.BatchNormalization(name="bn_1")(x)
    x = tf.keras.layers.ReLU(name="relu_1")(x)
    x = tf.keras.layers.Conv2D(
        12, (5, 3), strides=(2, 1), padding="same", use_bias=False,
        kernel_regularizer=regularizer, name="conv_2",
    )(x)
    x = tf.keras.layers.BatchNormalization(name="bn_2")(x)
    x = tf.keras.layers.ReLU(name="relu_2")(x)
    x = tf.keras.layers.Conv2D(
        16, (3, 3), strides=(2, 1), padding="same", use_bias=False,
        kernel_regularizer=regularizer, name="conv_3",
    )(x)
    x = tf.keras.layers.BatchNormalization(name="bn_3")(x)
    x = tf.keras.layers.ReLU(name="relu_3")(x)
    x = tf.keras.layers.AveragePooling2D(
        pool_size=(2, 2), name="local_average"
    )(x)
    x = tf.keras.layers.Flatten(name="flatten")(x)
    x = tf.keras.layers.Dense(
        32, activation="relu", kernel_regularizer=regularizer, name="dense"
    )(x)
    x = tf.keras.layers.Dropout(0.15, name="dropout")(x)
    outputs = tf.keras.layers.Dense(
        len(MODEL_LABELS), activation="softmax", name="probabilities"
    )(x)
    model = tf.keras.Model(inputs, outputs, name="r528_wake_query_kws")
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=5e-4),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
        weighted_metrics=[],
    )
    return model


def tflite_predict(interpreter, features, np):
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    raw_outputs = []
    probabilities = []
    output_scale, output_zero_point = output_details["quantization"]
    for feature in features:
        interpreter.set_tensor(
            input_details["index"], feature[np.newaxis].astype(np.int8)
        )
        interpreter.invoke()
        raw = interpreter.get_tensor(output_details["index"])[0].astype(np.int16)
        raw_outputs.append(raw)
        probabilities.append((raw - output_zero_point) * output_scale)
    return np.stack(raw_outputs), np.stack(probabilities)


def prediction_rows(rows, probabilities, raw_outputs, np):
    result = []
    for row, probability, raw in zip(rows, probabilities, raw_outputs, strict=True):
        predicted_index = int(np.argmax(probability))
        predicted_model_label = MODEL_LABELS[predicted_index]
        result.append(
            {
                "sample_id": row["sample_id"],
                "actual": model_label(row),
                "predicted": evaluation_label(predicted_model_label),
                "predicted_model_label": predicted_model_label,
                "scores_probability": {
                    label: round(float(probability[index]), 8)
                    for index, label in enumerate(MODEL_LABELS)
                },
                "scores_raw": {
                    label: int(raw[index])
                    for index, label in enumerate(MODEL_LABELS)
                },
            }
        )
    return result


def split_report(
    predictions: list[dict], thresholds_raw: dict[str, int]
) -> dict:
    return {
        "classification": classification_metrics(predictions),
        "threshold": threshold_metrics(predictions, thresholds_raw),
        "samples": predictions,
    }


def save_keras_model(model, output: Path) -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.stem}.", suffix=".keras", dir=output.parent
    )
    os.close(descriptor)
    temporary = Path(temporary_name)
    temporary.unlink()
    try:
        model.save(temporary)
        os.chmod(temporary, 0o600)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def train(corpus: Path, frontend_library: Path, output: Path, epochs: int, seed: int):
    os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
    os.environ.setdefault("TF_DETERMINISTIC_OPS", "1")
    import numpy as np
    import tensorflow as tf

    from kws_microfrontend import Microfrontend

    corpus = corpus.expanduser().resolve()
    if corpus.is_symlink() or not corpus.is_dir():
        raise ValueError("corpus must be a real directory")
    output = output.expanduser().resolve()
    if output == corpus or corpus not in output.parents:
        raise ValueError("model output must be a subdirectory of the corpus")
    if epochs < 1 or epochs > 500:
        raise ValueError("epochs must be between 1 and 500")
    output.mkdir(parents=True, exist_ok=True)

    tf.config.threading.set_inter_op_parallelism_threads(1)
    tf.config.threading.set_intra_op_parallelism_threads(1)
    tf.keras.utils.set_random_seed(seed)
    tf.config.experimental.enable_op_determinism()

    rows, selection_summary, selection_data = load_selection(corpus)
    frontend = Microfrontend(frontend_library)
    samples = []
    features = []
    feature_digest = hashlib.sha256()
    for row in rows:
        wav_path = corpus / row["wav"]
        wav_data = read_regular(wav_path, 1024 * 1024)
        if sha256(wav_data) != row["wav_sha256"]:
            raise ValueError(f"WAV SHA-256 mismatch for {row['sample_id']}")
        sample = frontend.read_wav(wav_path)
        feature, _ = frontend.extract_aligned(sample)
        if feature.shape != (178, 40):
            raise ValueError(
                f"unexpected feature shape for {row['sample_id']}: {feature.shape}"
            )
        feature_digest.update(row["sample_id"].encode("ascii"))
        feature_digest.update(b"\0")
        feature_digest.update(feature.tobytes())
        samples.append(sample)
        features.append(feature)
    all_samples = np.stack(samples)
    all_features = np.stack(features)
    all_labels = np.asarray([MODEL_LABELS.index(model_label(row)) for row in rows])

    split_data = {}
    split_samples = {}
    for split in ALLOWED_SPLITS:
        indices = [index for index, row in enumerate(rows) if row["split"] == split]
        split_samples[split] = all_samples[indices]
        split_data[split] = (
            all_features[indices][..., np.newaxis],
            all_labels[indices],
            [rows[index] for index in indices],
        )
    train_x, train_y, augmentation = balanced_training_set(
        split_samples["train"], split_data["train"][0][..., 0],
        split_data["train"][1], frontend, seed, np
    )
    validation_x, validation_y, _ = split_data["validation"]
    timing_data = {}
    for split in ("validation", "test"):
        _, _, split_rows = split_data[split]
        stress_x, stress_rows = timing_stress_set(
            split_samples[split], split_rows, frontend, np
        )
        timing_data[split] = (
            stress_x[..., np.newaxis],
            np.asarray(
                [
                    MODEL_LABELS.index(model_label(row))
                    for row in stress_rows
                ]
            ),
            stress_rows,
        )
    validation_stress_x, validation_stress_y, _ = timing_data["validation"]
    validation_stress_weights = balanced_sample_weights(
        validation_stress_y, np
    )

    model = build_model(train_x.shape[1:], tf)

    class ValidationGateSelector(tf.keras.callbacks.Callback):
        def __init__(self):
            super().__init__()
            self.best_key = None
            self.best_weights = None
            self.best_epoch = 0
            self.best_metrics = None

        def on_epoch_end(self, epoch, logs=None):
            probabilities = self.model(
                validation_stress_x.astype(np.float32), training=False
            ).numpy()
            metrics = validation_selection_metrics(
                validation_stress_y, probabilities, np
            )
            loss = float((logs or {}).get("val_loss", float("inf")))
            key = validation_selection_key(metrics, loss)
            if self.best_key is None or key > self.best_key:
                self.best_key = key
                self.best_weights = self.model.get_weights()
                self.best_epoch = epoch + 1
                self.best_metrics = metrics

        def restore(self):
            if self.best_weights is None:
                raise RuntimeError("validation selector did not observe an epoch")
            self.model.set_weights(self.best_weights)

    selector = ValidationGateSelector()
    callbacks = [
        selector,
        tf.keras.callbacks.ReduceLROnPlateau(
            monitor="val_loss", mode="min", factor=0.5, patience=10,
            min_delta=1e-4, min_lr=2e-5,
        ),
    ]
    history = model.fit(
        train_x.astype(np.float32),
        train_y,
        validation_data=(
            validation_stress_x.astype(np.float32), validation_stress_y,
            validation_stress_weights,
        ),
        epochs=epochs,
        batch_size=32,
        shuffle=True,
        callbacks=callbacks,
        verbose=0,
    )
    selector.restore()

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]

    def representative_dataset():
        sentinel = train_x[0].astype(np.float32).copy()
        sentinel.flat[0] = -128.0
        sentinel.flat[1] = 127.0
        yield [sentinel[np.newaxis]]
        for feature in train_x[: min(len(train_x), 256)]:
            yield [feature[np.newaxis].astype(np.float32)]

    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    model_data = converter.convert()
    interpreter = tf.lite.Interpreter(model_content=model_data, num_threads=1)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    input_scale, input_zero_point = input_details["quantization"]
    output_scale, output_zero_point = output_details["quantization"]

    evaluated_splits = {}
    evaluated_timing_splits = {}
    float_probabilities = []
    quantized_probabilities = []
    raw_by_split = {}
    probability_by_split = {}
    for split in ("validation", "test"):
        split_x, _, _ = timing_data[split]
        float_probability = model.predict(split_x.astype(np.float32), verbose=0)
        raw_output, quantized_probability = tflite_predict(interpreter, split_x, np)
        float_probabilities.append(float_probability)
        quantized_probabilities.append(quantized_probability)
        raw_by_split[split] = raw_output
        probability_by_split[split] = quantized_probability

    validation_timing_predictions = prediction_rows(
        timing_data["validation"][2],
        probability_by_split["validation"],
        raw_by_split["validation"],
        np,
    )
    validation_x, _, validation_rows = split_data["validation"]
    validation_raw, validation_probability = tflite_predict(
        interpreter, validation_x, np
    )
    validation_predictions = prediction_rows(
        validation_rows, validation_probability, validation_raw, np
    )
    thresholds_raw = choose_validation_thresholds(
        validation_predictions + validation_timing_predictions
    )
    thresholds = {
        label: {
            "raw_int8": raw,
            "probability": float(
                (raw - output_zero_point) * output_scale
                if raw <= 127 else 1.0
            ),
        }
        for label, raw in thresholds_raw.items()
    }
    for split in ("validation", "test"):
        timing_predictions = prediction_rows(
            timing_data[split][2], probability_by_split[split], raw_by_split[split], np
        )
        evaluated_timing_splits[split] = split_report(
            timing_predictions, thresholds_raw
        )
        split_x, _, split_rows = split_data[split]
        raw_output, quantized_probability = tflite_predict(
            interpreter, split_x, np
        )
        predictions = prediction_rows(
            split_rows, quantized_probability, raw_output, np
        )
        evaluated_splits[split] = split_report(predictions, thresholds_raw)

    float_all = np.concatenate(float_probabilities)
    quantized_all = np.concatenate(quantized_probabilities)
    argmax_agreement = float(
        np.mean(np.argmax(float_all, axis=1) == np.argmax(quantized_all, axis=1))
    )
    maximum_delta = float(np.max(np.abs(float_all - quantized_all)))
    operations = [details["op_name"] for details in interpreter._get_ops_details()]
    full_int8 = input_details["dtype"] == np.int8 and output_details["dtype"] == np.int8
    identity_input = abs(float(input_scale) - 1.0) < 1e-7 and input_zero_point == 0

    report = {
        "schema_version": SCHEMA_VERSION,
        "accepted": False,
        "labels": list(LABELS),
        "model_labels": list(MODEL_LABELS),
        "selection": {
            "sha256": sha256(selection_data),
            "samples": len(rows),
            "speakers": selection_summary["speakers"],
            "sessions": selection_summary["sessions"],
        },
        "frontend": {
            "library_sha256": sha256(read_regular(frontend.library_path, 8 * 1024 * 1024)),
            "features_sha256": feature_digest.hexdigest(),
            "shape": [178, 40, 1],
        },
        "training": {
            "architecture": MODEL_ARCHITECTURE,
            "seed": seed,
            "requested_epochs": epochs,
            "completed_epochs": len(history.history["loss"]),
            "best_validation_loss": min(float(value) for value in history.history["val_loss"]),
            "selected_epoch": selector.best_epoch,
            "selection_metrics": selector.best_metrics,
            "parameters": model.count_params(),
            "augmentation": augmentation,
            "tensorflow_version": tf.__version__,
        },
        "quantization": {
            "full_int8": bool(full_int8),
            "frontend_input_identity": bool(identity_input),
            "input": {"scale": float(input_scale), "zero_point": int(input_zero_point)},
            "output": {"scale": float(output_scale), "zero_point": int(output_zero_point)},
            "operations": operations,
        },
        "thresholds": {
            "by_label": thresholds,
            "selected_from": "evaluation.validation+timing_stress.validation",
            "decision_requires_argmax": True,
        },
        "quantization_agreement": {
            "samples": int(float_all.shape[0]),
            "argmax_agreement": argmax_agreement,
            "maximum_probability_delta": maximum_delta,
        },
        "evaluation": evaluated_splits,
        "timing_stress": {
            "onset_frames": list(TIMING_STRESS_ONSET_FRAMES),
            "onset_ms": [
                frame * 20 for frame in TIMING_STRESS_ONSET_FRAMES
            ],
            "frame_step_ms": 20,
            "validation": evaluated_timing_splits["validation"],
            "test": evaluated_timing_splits["test"],
        },
        "model": {
            "bytes": len(model_data),
            "sha256": sha256(model_data),
        },
        "limitations": [
            "All selected recordings are from one speaker (s001).",
            "Validation and test contain only a few utterances per intent.",
            "This is a personal prototype, not a speaker-independent KWS result.",
        ],
    }
    accepted, rejection_reasons = gate_report(report)
    report["accepted"] = accepted
    report["rejection_reasons"] = rejection_reasons

    history_data = {
        key: [float(value) for value in values] for key, values in history.history.items()
    }
    write_atomic(output / "kws_int8.tflite", model_data)
    write_atomic(
        output / "labels.txt", ("\n".join(MODEL_LABELS) + "\n").encode("ascii")
    )
    write_atomic(
        output / "training_history.json",
        (json.dumps(history_data, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    write_atomic(
        output / "evaluation.json",
        (json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode(
            "utf-8"
        ),
    )
    save_keras_model(model, output / "model_float.keras")
    acceptance_path = output / "MODEL_ACCEPTED.json"
    if accepted:
        write_atomic(
            acceptance_path,
            (json.dumps(
                {
                    "schema_version": SCHEMA_VERSION,
                    "model_sha256": report["model"]["sha256"],
                    "evaluation_sha256": sha256(
                        (output / "evaluation.json").read_bytes()
                    ),
                    "thresholds_raw_int8": thresholds_raw,
                },
                indent=2,
                sort_keys=True,
            ) + "\n").encode("utf-8"),
        )
    else:
        acceptance_path.unlink(missing_ok=True)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--frontend-library", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--epochs", type=int, default=120)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    args = parser.parse_args()
    output = args.output or args.corpus / "training/model"
    try:
        report = train(
            args.corpus, args.frontend_library, output, args.epochs, args.seed
        )
    except (ImportError, OSError, RuntimeError, ValueError) as error:
        print(f"KWS 训练失败：{error}", file=os.sys.stderr)
        return 1
    summary = {
        "accepted": report["accepted"],
        "model": report["model"],
        "thresholds": report["thresholds"],
        "validation": report["evaluation"]["validation"]["threshold"],
        "test": report["evaluation"]["test"]["threshold"],
        "timing_validation": report["timing_stress"]["validation"]["threshold"],
        "timing_test": report["timing_stress"]["test"]["threshold"],
        "rejection_reasons": report["rejection_reasons"],
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if report["accepted"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
