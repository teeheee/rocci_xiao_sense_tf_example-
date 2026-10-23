"""Train and export the XIAO Sense image classifier and its firmware header."""

from __future__ import annotations

import argparse
import json
import random
from pathlib import Path
from typing import Any

import numpy as np
import tensorflow as tf


IMAGE_HEIGHT = 96
IMAGE_WIDTH = 96
IMAGE_CHANNELS = 3
VALIDATION_FRACTION = 0.2
BATCH_SIZE = 4
DEFAULT_EPOCHS = 80


def load_dataset(data_dir: Path, seed: int) -> tuple[list[str], np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    class_dirs = sorted(path for path in data_dir.iterdir() if path.is_dir())
    if len(class_dirs) < 2:
        raise ValueError(f"Expected at least two class folders in {data_dir}")

    rng = random.Random(seed)
    class_names: list[str] = []
    train_images: list[np.ndarray] = []
    train_labels: list[int] = []
    validation_images: list[np.ndarray] = []
    validation_labels: list[int] = []

    for class_index, class_dir in enumerate(class_dirs):
        files = sorted(class_dir.glob("*.npy"))
        if len(files) < 2:
            raise ValueError(
                f"Class '{class_dir.name}' needs at least two .npy images; found {len(files)}"
            )

        rng.shuffle(files)
        validation_count = max(1, round(len(files) * VALIDATION_FRACTION))
        class_names.append(class_dir.name)

        for position, image_path in enumerate(files):
            try:
                image = np.load(image_path, allow_pickle=False)
            except (OSError, ValueError) as error:
                raise ValueError(f"Could not load image {image_path}: {error}") from error

            expected_shape = (IMAGE_HEIGHT, IMAGE_WIDTH, IMAGE_CHANNELS)
            if image.shape != expected_shape or image.dtype != np.uint8:
                raise ValueError(
                    f"{image_path} must be uint8 {expected_shape}; "
                    f"got {image.dtype} {image.shape}"
                )

            if position < validation_count:
                validation_images.append(image)
                validation_labels.append(class_index)
            else:
                train_images.append(image)
                train_labels.append(class_index)

    return (
        class_names,
        np.stack(train_images),
        np.asarray(train_labels, dtype=np.int32),
        np.stack(validation_images),
        np.asarray(validation_labels, dtype=np.int32),
    )


def create_model(class_count: int) -> tf.keras.Model:
    return tf.keras.Sequential(
        [
            tf.keras.layers.Input(
                shape=(IMAGE_HEIGHT, IMAGE_WIDTH, IMAGE_CHANNELS), name="image"
            ),
            tf.keras.layers.Rescaling(1.0 / 255.0),
            tf.keras.layers.Conv2D(8, 3, padding="same", activation="relu"),
            tf.keras.layers.MaxPooling2D(pool_size=2),
            tf.keras.layers.Conv2D(16, 3, padding="same", activation="relu"),
            tf.keras.layers.GlobalAveragePooling2D(),
            tf.keras.layers.Dense(class_count, activation="softmax"),
        ]
    )


def create_training_dataset(images: np.ndarray, labels: np.ndarray, seed: int) -> tf.data.Dataset:
    dataset = tf.data.Dataset.from_tensor_slices((images, labels))
    dataset = dataset.shuffle(len(images), seed=seed, reshuffle_each_iteration=True)

    def augment(image: tf.Tensor, label: tf.Tensor) -> tuple[tf.Tensor, tf.Tensor]:
        image = tf.cast(image, tf.float32)
        image = tf.image.random_flip_left_right(image)
        image = tf.image.random_brightness(image, max_delta=18.0)
        image = tf.image.random_contrast(image, lower=0.85, upper=1.15)
        image = tf.clip_by_value(image, 0.0, 255.0)
        return image, label

    dataset = dataset.map(augment, num_parallel_calls=1)
    dataset = dataset.batch(BATCH_SIZE).prefetch(1)
    options = tf.data.Options()
    options.deterministic = True
    return dataset.with_options(options)


def write_model_header(model_bytes: bytes, class_names: list[str], output_path: Path) -> None:
    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "",
        f"static const char *const kClassNames[] = {{{', '.join(json.dumps(name) for name in class_names)}}};",
        f"static constexpr size_t kClassCount = {len(class_names)};",
        "",
        "alignas(16) const unsigned char g_model[] = {",
    ]

    for offset in range(0, len(model_bytes), 12):
        chunk = model_bytes[offset : offset + 12]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")

    lines.extend(
        [
            "};",
            "static constexpr size_t g_model_len = sizeof(g_model);",
            "",
        ]
    )
    output_path.write_text("\n".join(lines), encoding="utf-8")


def train_and_export(data_dir: Path, epochs: int, seed: int) -> dict[str, Any]:
    tf.keras.utils.set_random_seed(seed)
    class_names, train_images, train_labels, validation_images, validation_labels = (
        load_dataset(data_dir, seed)
    )

    print(f"Classes: {', '.join(class_names)}")
    for class_index, class_name in enumerate(class_names):
        train_count = int(np.count_nonzero(train_labels == class_index))
        validation_count = int(np.count_nonzero(validation_labels == class_index))
        print(f"  {class_name}: {train_count} training, {validation_count} validation")

    model = create_model(len(class_names))
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )

    class_counts = np.bincount(train_labels, minlength=len(class_names))
    class_weights = {
        class_index: len(train_labels) / (len(class_names) * int(count))
        for class_index, count in enumerate(class_counts)
    }
    training_dataset = create_training_dataset(train_images, train_labels, seed)
    validation_dataset = (
        tf.data.Dataset.from_tensor_slices((validation_images, validation_labels))
        .batch(BATCH_SIZE)
    )
    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor="val_loss", patience=15, restore_best_weights=True
        )
    ]

    model.fit(
        training_dataset,
        validation_data=validation_dataset,
        epochs=epochs,
        class_weight=class_weights,
        callbacks=callbacks,
        verbose=2,
    )
    validation_loss, validation_accuracy = model.evaluate(
        validation_dataset, verbose=0
    )

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = lambda: (
        [image[np.newaxis, ...].astype(np.float32)]
        for image in train_images
    )
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.uint8
    converter.inference_output_type = tf.uint8
    model_bytes = converter.convert()

    interpreter = tf.lite.Interpreter(model_content=model_bytes, num_threads=1)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    if input_details["dtype"] != np.uint8 or output_details["dtype"] != np.uint8:
        raise RuntimeError("The exported model must have uint8 input and output tensors")

    correct_predictions = 0
    for image, expected_label in zip(validation_images, validation_labels):
        interpreter.set_tensor(input_details["index"], image[np.newaxis, ...])
        interpreter.invoke()
        output = interpreter.get_tensor(output_details["index"])[0]
        if output.shape != (len(class_names),):
            raise RuntimeError("The exported model output does not match the class count")
        correct_predictions += int(np.argmax(output) == expected_label)
    tflite_validation_accuracy = correct_predictions / len(validation_labels)

    project_dir = Path(__file__).resolve().parent
    model_dir = project_dir / "model"
    model_dir.mkdir(parents=True, exist_ok=True)
    model_path = model_dir / "xiao_sense_classifier.tflite"
    model_path.write_bytes(model_bytes)
    write_model_header(model_bytes, class_names, project_dir / "src" / "model_data.h")

    report: dict[str, Any] = {
        "classes": class_names,
        "training_images": int(len(train_images)),
        "validation_images": int(len(validation_images)),
        "validation_accuracy": float(validation_accuracy),
        "tflite_validation_accuracy": float(tflite_validation_accuracy),
        "validation_loss": float(validation_loss),
        "model_bytes": len(model_bytes),
        "input_shape": [int(dimension) for dimension in input_details["shape"]],
        "input_dtype": str(input_details["dtype"]),
        "output_dtype": str(output_details["dtype"]),
    }
    (model_dir / "training_report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    return report


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Train the image classifier and export a quantized TensorFlow Lite model."
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "captures",
        help="Folder with one subfolder of .npy images per class.",
    )
    parser.add_argument("--epochs", type=int, default=DEFAULT_EPOCHS)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()
    if args.epochs < 1:
        parser.error("--epochs must be at least 1")

    report = train_and_export(args.data_dir, args.epochs, args.seed)
    print(
        f"Exported {report['model_bytes']} bytes; "
        f"TFLite holdout accuracy {report['tflite_validation_accuracy']:.1%}"
    )
    print("Wrote model/xiao_sense_classifier.tflite and src/model_data.h")


if __name__ == "__main__":
    main()
