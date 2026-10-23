# XIAO Sense TensorFlow Lite classifier

This example classifies the `.npy` camera frames in `captures/`. Each class is
stored in its own folder. The current labels are `bridge`, `empty`, and `flat`.

## Train and export

Use Python 3.9–3.11, then create an isolated environment and install the pinned
training dependency:

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python train_model.py
```

The single training script validates the recorded 96x96 RGB arrays, creates a
stratified holdout split, and applies random horizontal flips, brightness
changes, and contrast changes to training images only. It trains a small
two-convolution classifier and exports a fully quantized uint8 TensorFlow Lite
model. The holdout set is very small, so its accuracy is only a rough check;
record more examples per class before relying on the classifier.

Training writes:

- `model/xiao_sense_classifier.tflite` — standalone TensorFlow Lite model.
- `model/training_report.json` — split counts, model size, and holdout metrics.
- `src/model_data.h` — model bytes and class labels used by the firmware.

To use a different capture folder or training duration, run
`python train_model.py --data-dir path\to\captures --epochs 100`.

## Run on the camera

Build and upload this PlatformIO project for the Seeed XIAO ESP32S3 Sense.
The firmware converts each 96x96 RGB565 camera frame to RGB uint8, invokes the
quantized model using the ESP32-S3 TensorFlow Lite Micro runtime, and prints the
predicted class and confidence to the serial terminal. The PlatformIO
configuration pins the Arduino TFLite Micro library and includes compatibility
flags for the current ESP32 toolchain.

After changing the classes, capture files, or training settings, run the
training script again so the model and generated firmware header stay in sync.
