#!/usr/bin/env python3
"""Train and export the local EMNIST uppercase-letter neural classifier."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import struct
from pathlib import Path

import numpy as np


INPUT_SIZE = 28 * 28
HIDDEN_SIZE = 128
OUTPUT_SIZE = 26
SEED = 20260808


def read_idx(path: Path) -> np.ndarray:
    raw = gzip.open(path, "rb").read()
    magic = struct.unpack_from(">I", raw)[0]
    if magic == 0x00000801:
        count = struct.unpack_from(">I", raw, 4)[0]
        return np.frombuffer(raw, dtype=np.uint8, offset=8).reshape(count)
    if magic == 0x00000803:
        count, rows, columns = struct.unpack_from(">III", raw, 4)
        return np.frombuffer(raw, dtype=np.uint8, offset=16).reshape(count, rows, columns)
    raise RuntimeError(f"unsupported IDX magic 0x{magic:08x}: {path}")


def load_split(root: Path, split: str) -> tuple[np.ndarray, np.ndarray]:
    images = read_idx(root / f"emnist-letters-{split}-images-idx3-ubyte.gz")
    labels = read_idx(root / f"emnist-letters-{split}-labels-idx1-ubyte.gz")
    # EMNIST's IDX storage is transposed relative to its visual orientation.
    images = images.transpose(0, 2, 1).copy()
    labels = labels.astype(np.int64) - 1
    return images, labels


def binarize(images: np.ndarray, rng: np.random.Generator, training: bool) -> np.ndarray:
    if training:
        threshold = rng.integers(36, 112, size=(images.shape[0], 1, 1), dtype=np.uint8)
        result = images >= threshold
        shifts_x = rng.integers(-2, 3, size=images.shape[0])
        shifts_y = rng.integers(-2, 3, size=images.shape[0])
        shifted = np.zeros_like(result)
        for index, (dx, dy) in enumerate(zip(shifts_x, shifts_y)):
            source_x0, source_x1 = max(0, -dx), min(28, 28 - dx)
            source_y0, source_y1 = max(0, -dy), min(28, 28 - dy)
            shifted[index, source_y0 + dy : source_y1 + dy,
                    source_x0 + dx : source_x1 + dx] = result[
                        index, source_y0:source_y1, source_x0:source_x1
                    ]
        result = shifted
    else:
        result = images >= 64
    return result.reshape(images.shape[0], INPUT_SIZE).astype(np.float32)


def softmax_cross_entropy(logits: np.ndarray, labels: np.ndarray) -> tuple[float, np.ndarray]:
    shifted = logits - logits.max(axis=1, keepdims=True)
    probabilities = np.exp(shifted)
    probabilities /= probabilities.sum(axis=1, keepdims=True)
    loss = -np.log(probabilities[np.arange(labels.size), labels] + 1e-8).mean()
    probabilities[np.arange(labels.size), labels] -= 1.0
    probabilities /= labels.size
    return float(loss), probabilities


def evaluate(images: np.ndarray, labels: np.ndarray,
             w1: np.ndarray, b1: np.ndarray,
             w2: np.ndarray, b2: np.ndarray) -> tuple[float, np.ndarray]:
    predictions: list[np.ndarray] = []
    for start in range(0, images.shape[0], 2048):
        x = images[start : start + 2048]
        hidden = np.maximum(0.0, x @ w1 + b1)
        predictions.append(np.argmax(hidden @ w2 + b2, axis=1))
    predicted = np.concatenate(predictions)
    confusion = np.zeros((OUTPUT_SIZE, OUTPUT_SIZE), dtype=np.int64)
    np.add.at(confusion, (labels, predicted), 1)
    return float((predicted == labels).mean()), confusion


def train(train_images: np.ndarray, train_labels: np.ndarray,
          test_images: np.ndarray, test_labels: np.ndarray,
          epochs: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, list[dict]]:
    rng = np.random.default_rng(SEED)
    w1 = (rng.standard_normal((INPUT_SIZE, HIDDEN_SIZE), dtype=np.float32) *
          np.sqrt(2.0 / INPUT_SIZE)).astype(np.float32)
    b1 = np.zeros(HIDDEN_SIZE, dtype=np.float32)
    w2 = (rng.standard_normal((HIDDEN_SIZE, OUTPUT_SIZE), dtype=np.float32) *
          np.sqrt(2.0 / HIDDEN_SIZE)).astype(np.float32)
    b2 = np.zeros(OUTPUT_SIZE, dtype=np.float32)
    params = [w1, b1, w2, b2]
    moments = [np.zeros_like(value) for value in params]
    variances = [np.zeros_like(value) for value in params]
    step = 0
    report: list[dict] = []

    test_x = binarize(test_images, rng, False)
    batch_size = 1024
    for epoch in range(1, epochs + 1):
        order = rng.permutation(train_labels.size)
        losses: list[float] = []
        for start in range(0, order.size, batch_size):
            indices = order[start : start + batch_size]
            x = binarize(train_images[indices], rng, True)
            labels = train_labels[indices]
            hidden_pre = x @ w1 + b1
            hidden = np.maximum(0.0, hidden_pre)
            logits = hidden @ w2 + b2
            loss, gradient = softmax_cross_entropy(logits, labels)
            losses.append(loss)
            dw2 = hidden.T @ gradient + 1e-5 * w2
            db2 = gradient.sum(axis=0)
            dhidden = gradient @ w2.T
            dhidden[hidden_pre <= 0] = 0
            dw1 = x.T @ dhidden + 1e-5 * w1
            db1 = dhidden.sum(axis=0)
            gradients = [dw1, db1, dw2, db2]
            step += 1
            learning_rate = 0.0015 * (0.88 ** (epoch - 1))
            for parameter, grad, moment, variance in zip(
                    params, gradients, moments, variances):
                moment *= 0.9
                moment += 0.1 * grad
                variance *= 0.999
                variance += 0.001 * grad * grad
                corrected_m = moment / (1.0 - 0.9 ** step)
                corrected_v = variance / (1.0 - 0.999 ** step)
                parameter -= learning_rate * corrected_m / (np.sqrt(corrected_v) + 1e-7)
        accuracy, _ = evaluate(test_x, test_labels, w1, b1, w2, b2)
        row = {"epoch": epoch, "loss": float(np.mean(losses)), "accuracy": accuracy}
        report.append(row)
        print(f"epoch {epoch:02d}: loss={row['loss']:.4f} test={accuracy:.4%}")
    return w1, b1, w2, b2, report


def quantize_columns(weights: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    scales = np.max(np.abs(weights), axis=0) / 127.0
    scales[scales == 0] = 1.0
    quantized = np.clip(np.rint(weights / scales), -127, 127).astype(np.int8)
    return quantized, scales.astype(np.float32)


def evaluate_quantized(images: np.ndarray, labels: np.ndarray,
                       q1: np.ndarray, s1: np.ndarray, b1: np.ndarray,
                       q2: np.ndarray, s2: np.ndarray, b2: np.ndarray) -> tuple[float, np.ndarray]:
    predictions: list[np.ndarray] = []
    input_u8 = images.astype(np.uint8)
    for start in range(0, images.shape[0], 2048):
        x = input_u8[start : start + 2048].astype(np.int32)
        hidden = np.maximum(0.0, (x @ q1.astype(np.int32)) * s1 + b1)
        logits = (hidden @ q2.astype(np.float32)) * s2 + b2
        predictions.append(np.argmax(logits, axis=1))
    predicted = np.concatenate(predictions)
    confusion = np.zeros((OUTPUT_SIZE, OUTPUT_SIZE), dtype=np.int64)
    np.add.at(confusion, (labels, predicted), 1)
    return float((predicted == labels).mean()), confusion


def format_array(values: np.ndarray, formatter, per_line: int) -> str:
    flat = values.reshape(-1)
    lines = []
    for start in range(0, flat.size, per_line):
        lines.append("    " + ", ".join(formatter(value) for value in flat[start:start + per_line]) + ",")
    return "\n".join(lines)


def export_model(output: Path, q1: np.ndarray, s1: np.ndarray, b1: np.ndarray,
                 q2: np.ndarray, s2: np.ndarray, b2: np.ndarray,
                 accuracy: float) -> None:
    model_bytes = b"".join((q1.tobytes(), s1.tobytes(), b1.tobytes(),
                            q2.tobytes(), s2.tobytes(), b2.tobytes()))
    digest = hashlib.sha256(model_bytes).hexdigest()
    output.write_text(
        "\n".join((
            "#pragma once",
            "",
            "#include <stdint.h>",
            "",
            "// Generated by tools/train-handwriting-neural.py from NIST EMNIST Letters.",
            f"constexpr uint16_t kHandwritingNeuralInputSize = {INPUT_SIZE};",
            f"constexpr uint8_t kHandwritingNeuralHiddenSize = {HIDDEN_SIZE};",
            f"constexpr uint8_t kHandwritingNeuralOutputSize = {OUTPUT_SIZE};",
            f"constexpr float kHandwritingNeuralTestAccuracy = {accuracy:.8f}f;",
            f'constexpr char kHandwritingNeuralSha256[] = "{digest}";',
            "constexpr int8_t kHandwritingNeuralHiddenWeights[] = {",
            format_array(q1.T, lambda value: str(int(value)), 24),
            "};",
            "constexpr float kHandwritingNeuralHiddenScales[] = {",
            format_array(s1, lambda value: f"{float(value):.9g}f", 8),
            "};",
            "constexpr float kHandwritingNeuralHiddenBiases[] = {",
            format_array(b1, lambda value: f"{float(value):.9g}f", 8),
            "};",
            "constexpr int8_t kHandwritingNeuralOutputWeights[] = {",
            format_array(q2.T, lambda value: str(int(value)), 24),
            "};",
            "constexpr float kHandwritingNeuralOutputScales[] = {",
            format_array(s2, lambda value: f"{float(value):.9g}f", 8),
            "};",
            "constexpr float kHandwritingNeuralOutputBiases[] = {",
            format_array(b2, lambda value: f"{float(value):.9g}f", 8),
            "};",
            "",
        )),
        encoding="ascii",
        newline="\n",
    )
    print(f"exported {output} ({output.stat().st_size} bytes, sha256={digest})")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=18)
    args = parser.parse_args()
    train_images, train_labels = load_split(args.data, "train")
    test_images, test_labels = load_split(args.data, "test")
    print(f"EMNIST Letters train={train_labels.size} test={test_labels.size}")
    w1, b1, w2, b2, epochs = train(
        train_images, train_labels, test_images, test_labels, args.epochs
    )
    q1, s1 = quantize_columns(w1)
    q2, s2 = quantize_columns(w2)
    test_x = binarize(test_images, np.random.default_rng(SEED), False)
    accuracy, confusion = evaluate_quantized(test_x, test_labels, q1, s1, b1, q2, s2, b2)
    print(f"quantized test={accuracy:.4%}")
    for left, right in (("R", "K"), ("Y", "T")):
        a, b = ord(left) - 65, ord(right) - 65
        print(f"{left}->{right}={confusion[a,b]} {right}->{left}={confusion[b,a]}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    export_model(args.output, q1, s1, b1, q2, s2, b2, accuracy)
    args.report.write_text(json.dumps({
        "schema": 1,
        "dataset": "NIST EMNIST Letters",
        "seed": SEED,
        "architecture": [INPUT_SIZE, HIDDEN_SIZE, OUTPUT_SIZE],
        "epochs": epochs,
        "quantizedTestAccuracy": accuracy,
        "confusion": confusion.tolist(),
    }, indent=2) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
