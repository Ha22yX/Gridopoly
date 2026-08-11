# Player Console Neural Handwriting Recognition

The player console recognizes uppercase `A-Z` locally. It does not send handwriting,
stroke coordinates, or player names to the Raspberry Pi for recognition.

## Runtime pipeline

1. Preserve physical pen lifts as separate strokes.
2. Wait until the handwriting area has received no touch for 1.2 seconds.
3. Find the stroke bounding box and scale it proportionally into a centered `28 x 28`
   binary raster. Stroke samples are connected only according to the capture state machine.
4. Evaluate a quantized `784 -> 128 ReLU -> 26` multilayer perceptron.
5. Use the neural result when its top-logit margin is at least `0.45`; otherwise use the
   existing stroke-aware template and direction classifier as a fallback.

The network contains 103,680 int8 weights plus 308 float scales and biases. The
generated model header adds about 105 KiB of flash-resident model data and uses a
fixed workspace of approximately 1.5 KiB at runtime. No allocator, Wi-Fi request,
or server round trip is involved in recognition.

## Dataset and measured result

The reproducible trainer uses the official NIST EMNIST Letters split:

- 124,800 training samples
- 20,800 test samples
- 26 balanced uppercase/lowercase letter classes mapped to uppercase `A-Z`
- deterministic seed `20260808`
- random threshold and up-to-two-pixel translation augmentation during training

The exported int8 model reaches `87.0385%` on the complete 20,800-sample test set.
The model SHA-256 embedded in the firmware is:

```text
cf9aca920784ded15a14bceea271d1848246ee6bdf7689b4d31b43e80fd4cedc
```

The firmware SelfTest rejects a model below `87%` test accuracy and also exercises
device-coordinate `R/K` and `Y/T` samples through the complete combined classifier.

## Reproduction

NumPy is the only non-standard Python dependency. Dataset files are not committed.
The fetcher reads the official aggregate NIST ZIP directory and downloads only the
four EMNIST Letters gzip members with HTTP range requests, then verifies each ZIP CRC.

From the repository root:

```powershell
python .\Firmware\PlayerConsole\tools\fetch-emnist-letters.py `
  --output .\.tmp\emnist

python .\Firmware\PlayerConsole\tools\train-handwriting-neural.py `
  --data .\.tmp\emnist `
  --epochs 18 `
  --output .\Firmware\PlayerConsole\ui_handwriting_neural_model.h `
  --report .\.tmp\emnist\training-report.json
```

The generated `ui_handwriting_neural_model.h` is committed so ordinary Arduino
builds do not need Python, NumPy, the training dataset, or network access.
