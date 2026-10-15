# Player Console Neural Handwriting Recognition

The player console recognizes uppercase `A-Z` locally. It does not send handwriting,
stroke coordinates, or player names to the Raspberry Pi for recognition.

## Runtime pipeline (documentation checked October 15, 2026)

1. Capture physical touch samples and contact boundaries in the input driver. A slow UI loop does not split a continuous stroke merely because samples are processed more than 100 ms apart; actual release, leaving the canvas or unavailable samples break contact.
2. Retain the canvas through same-page Tag/Identity updates. Use real sampling/release timestamps and require 1.2 seconds of inactivity before automatic submission; failed touch reads cannot trigger submission.
3. Recognize clear general geometric/stroke matches first. Otherwise evaluate the existing quantized `784 -> 128 ReLU -> 26` network on a centered 28×28 raster, using the existing 0.45 logit-margin gate and stroke-aware template fallback. Model weights did not change in this repair.
4. Recognition runs in the existing background worker with generation checks. Rejected strokes remain for correction. DELETE cancels current ink/results before deleting an existing character; DONE waits for the last ink result before returning to Name Review.
5. The input dispatcher processes at most 16 ordered events per application iteration and yields after ButtonUp so handwriting/page work handles the release before the next batch. Pending input postpones hold detection, preventing a queued short release from becoming a false long hold. This change does not alter the 1.2-second physical-touch inactivity rule.

The user confirmed handwriting after the September repair deployment; see the [input repair record](player-input-handwriting-fix-2026-09-26.md). That evidence does not certify all four October devices. Current COM7 22ca and COM27/28/29 dfc4 include the later input batching/focus work; current device versions and remaining handwriting, rotary and LCD checks are tracked in the [player baseline](../player-console/current-player-console-baseline.md) and [October input report](player-16-trial-progress-2026-10-15.md). This documentation update does not retrain the model, rerun tests or operate devices. Historical EMNIST accuracy below is a dataset result, not measured on-device user accuracy.

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
