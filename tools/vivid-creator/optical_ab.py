#!/usr/bin/env python3
"""VIVID stage 7AD: bounded, dependency-free optical viewport A/B report.

Reports RGB channel changes in a fixed Creator viewport crop. Does not infer
which material caused a difference or claim physically correct lighting.
"""
import argparse
import json
from pathlib import Path


def read_ppm(path: Path):
    with path.open("rb") as stream:
        header = [stream.readline().strip() for _ in range(3)]
        if header[0] != b"P6" or header[2] != b"255":
            raise ValueError(f"{path}: expected P6 RGB maxval 255")
        dims = header[1].split()
        if len(dims) != 2:
            raise ValueError(f"{path}: invalid dimensions")
        width, height = map(int, dims)
        if not (600 <= width <= 16384 and 400 <= height <= 16384):
            raise ValueError(f"{path}: unexpected image dimensions")
        payload = stream.read()
    if len(payload) != width * height * 3:
        raise ValueError(f"{path}: invalid pixel buffer size")
    return width, height, payload


def compare(first: Path, second: Path, threshold: int):
    w, h, a = read_ppm(first)
    bw, bh, b = read_ppm(second)
    if (w, h) != (bw, bh):
        raise ValueError("A/B images have different dimensions")
    left, right = max(420, w // 3), min(w - 16, w * 9 // 10)
    top, bottom = h // 8, h * 7 // 8
    if right <= left or bottom <= top:
        raise ValueError("invalid viewport comparison crop")
    pixels = (right - left) * (bottom - top)
    absolute = [0, 0, 0]
    changed_channel = [0, 0, 0]
    changed_rgb = 0
    largest = 0
    for y in range(top, bottom):
        for x in range(left, right):
            k = (y * w + x) * 3
            diff = [abs(a[k + channel] - b[k + channel])
                    for channel in range(3)]
            total = sum(diff)
            for channel in range(3):
                absolute[channel] += diff[channel]
                changed_channel[channel] += diff[channel] > threshold
            changed_rgb += total > threshold
            largest = max(largest, total)
    return {
        "image_size": [w, h],
        "viewport_crop": [left, top, right, bottom],
        "samples": pixels,
        "threshold": threshold,
        "changed_pixels": changed_rgb,
        "changed_fraction": changed_rgb / pixels,
        "mean_abs_rgb": [value / pixels for value in absolute],
        "changed_per_channel": changed_channel,
        "max_total_rgb_delta": largest,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--threshold", type=int, default=24)
    parser.add_argument("--min-changed", type=int, default=400)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not 0 <= args.threshold <= 765 or args.min_changed < 0:
        parser.error("threshold must be 0..765 and min-changed >= 0")
    try:
        result = compare(args.reference, args.candidate, args.threshold)
        result["reference"] = str(args.reference)
        result["candidate"] = str(args.candidate)
        result["min_changed"] = args.min_changed
        result["pass"] = result["changed_pixels"] >= args.min_changed
        report = json.dumps(result, indent=2, sort_keys=True)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(report + "\n", encoding="utf-8")
        print(report)
        if not result["pass"]:
            raise ValueError("insufficient viewport pixel difference")
    except (OSError, ValueError) as exc:
        parser.exit(1, f"VIVID optical A/B FAIL: {exc}\n")


if __name__ == "__main__":
    main()
