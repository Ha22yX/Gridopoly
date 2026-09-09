"""Summarize saved SelfTest logs; never opens a device or changes pass criteria."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re


SUMMARY = re.compile(
    r"^(CAROUSEL|LIST) PERF (\w+) frames=(\d+) fps=(\d+) "
    r"first_frame_ms=(\d+) max_gap_ms=(\d+) coverage_ms=(\d+) "
    r"incremental=(\d+) rebuild=(\d+) (PASS|FAIL)$"
)
TRACE = re.compile(r"^(CAROUSEL|LIST) PERF (\w+) DIAG (.*)$")
SAMPLE = re.compile(r"\[(\d+):(\d+)/(\d+)(?: s=(\d+) w=(\d+)(?: c=(\d+) r=(\d+) g=(\d+))?)?\]")


def analyze(text, scan_ms):
    scenes = {}
    for line in text.splitlines():
        match = SUMMARY.fullmatch(line.strip())
        if match:
            kind, name, frames, fps, first, gap, coverage, incremental, rebuild, status = match.groups()
            scenes[(kind, name)] = {
                "kind": kind, "name": name, "reported_status": status,
                "frames": int(frames), "fps": int(fps),
                "first_frame_ms": int(first), "max_gap_ms": int(gap),
                "coverage_ms": int(coverage), "incremental": int(incremental),
                "rebuild": int(rebuild), "samples": [],
            }
            continue
        match = TRACE.fullmatch(line.strip())
        if not match:
            continue
        kind, name, raw = match.groups()
        scene = scenes.get((kind, name))
        if scene is None:
            continue
        for gap, refresh, pixels, submit, wait, copy, rect, glyph in SAMPLE.findall(raw):
            sample = {"gap_ms": int(gap), "refresh_ms_including_wait": int(refresh),
                      "redrawn_pixels": int(pixels)}
            if submit:
                sample.update(submit_us=int(submit), wait_us=int(wait))
            if copy:
                sample.update(copy_us=int(copy), rect_us=int(rect), glyph_us=int(glyph))
            scene["samples"].append(sample)
    for scene in scenes.values():
        # First-frame latency is measured from the input, not the previous scan.
        steady = scene["samples"][1:]
        scene["steady_gap_histogram_ms"] = dict(sorted(Counter(
            sample["gap_ms"] for sample in steady).items()))
        scene["estimated_scan_intervals"] = dict(sorted(Counter(
            max(1, round(sample["gap_ms"] / scan_ms)) for sample in steady).items()))
        scene["timing_breakdown_available"] = bool(scene["samples"]) and all(
            "wait_us" in sample for sample in scene["samples"])
    if not scenes:
        raise ValueError("No complete CAROUSEL/LIST PERF summary found in log")
    return {"assumed_scan_ms": scan_ms,
            "notes": ["Scan intervals are estimates from the supplied period.",
                      "LVGL monitor duration includes layout, sync, draw and flush wait.",
                      "Missing submit/wait fields are unavailable, not zero.",
                      "Copy/rect/glyph counters span consecutive completed last-flush boundaries; they are not an exact decomposition of the monitor duration on that line.",
                      "Reported PASS/FAIL is preserved; no acceptance thresholds change."],
            "scenes": list(scenes.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--scan-ms", type=float, required=True,
                        help="Configured/observed panel scan period in milliseconds")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.scan_ms <= 0:
        parser.error("--scan-ms must be positive")
    try:
        report = analyze(args.log.read_text(encoding="utf-8-sig", errors="replace"), args.scan_ms)
    except ValueError as error:
        parser.error(str(error))
    output = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(output, encoding="utf-8")
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
