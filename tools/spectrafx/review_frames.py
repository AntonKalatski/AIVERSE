#!/usr/bin/env python3
"""Review actual SpectraFX/PRISMA Metal P6 screenshots without Pillow.

Reads seven screenshots from the user's on-device capture (never fabricates
missing frames), writes one comparison contact sheet PPM plus JSON metrics.
The contact sheet contains no in-image text; layout is written to JSON.
"""
import argparse
import json
from pathlib import Path

NAMES = (
    "baseline-a", "effect-a", "baseline-b", "effect-b",
    "after-reset-a", "prisma-baseline-a", "prisma-effect-a",
)
LAYOUT = (
    ("baseline-a", "effect-a", "delta-a", "after-reset-a"),
    ("baseline-b", "effect-b", "delta-b", None),
    ("prisma-baseline-a", "prisma-effect-a", "prisma-delta-a", None),
)
PAD = 4
BG = (23, 25, 30)


def load_ppm(path):
    """Decode an 8-bit P6 file; pixel bytes are never treated as whitespace."""
    with Path(path).open("rb") as f:
        def token():
            char = f.read(1)
            while char and (char in b" \t\r\n" or char == b"#"):
                if char == b"#":
                    f.readline()
                char = f.read(1)
            if not char:
                raise ValueError(f"{path}: truncated PPM header")
            buf = bytearray()
            while char and char not in b" \t\r\n":
                if char == b"#":
                    raise ValueError(f"{path}: comment inside token")
                buf.extend(char)
                char = f.read(1)
            if not char:
                raise ValueError(f"{path}: missing binary-data separator")
            return bytes(buf)

        if token() != b"P6":
            raise ValueError(f"{path}: expected P6 binary RGB")
        try:
            width, height, maximum = (int(token()) for _ in range(3))
        except (TypeError, ValueError) as exc:
            raise ValueError(f"{path}: invalid dimensions/maxval") from exc
        if width <= 0 or height <= 0 or width * height > 10_000_000:
            raise ValueError(f"{path}: unsupported image size")
        if maximum != 255:
            raise ValueError(f"{path}: maxval must equal 255")
        pixels = f.read()
        if len(pixels) != width * height * 3:
            raise ValueError(f"{path}: expected {width * height * 3} RGB bytes; got {len(pixels)}")
    return width, height, pixels


def write_ppm(path, width, height, pixels):
    if len(pixels) != width * height * 3:
        raise ValueError("contact-sheet payload size mismatch")
    with Path(path).open("wb") as f:
        f.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        f.write(pixels)


def metric(base, effect, width, height):
    changed = 0
    positive_red = 0
    weighted_x = 0.0
    weight = 0.0
    max_red = 0
    max_rgb = 0
    abs_sum = 0
    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 3
            r = int(effect[i]) - int(base[i])
            delta = [abs(int(effect[i + c]) - int(base[i + c])) for c in range(3)]
            max_red = max(max_red, abs(r))
            max_rgb = max(max_rgb, *delta)
            abs_sum += sum(delta)
            if r > 20 and 8 <= x < width - 8 and 8 <= y < height - 8:
                positive_red += 1
                weighted_x += x * r
                weight += r
            if max(delta) > 12:
                changed += 1
    center = ((height // 2) * width + width // 2) * 3
    return {
        "changed_rgb_pixels_gt12": changed,
        "positive_red_pixels_gt20_inner": positive_red,
        "positive_red_centroid_x": round(weighted_x / weight, 3) if weight else None,
        "maximum_absolute_red_delta": max_red,
        "maximum_absolute_rgb_delta": max_rgb,
        "mean_absolute_rgb_delta": round(abs_sum / (width * height * 3), 4),
        "center_red_delta": int(effect[center]) - int(base[center]),
    }


def delta_frame(base, effect):
    return bytes(min(255, 4 * abs(int(a) - int(b))) for a, b in zip(base, effect))


def analyze(prefix, output_prefix, strict=False):
    frames = {}
    width = height = None
    for name in NAMES:
        source = Path(f"{prefix}-{name}.ppm")
        w, h, px = load_ppm(source)
        if width is None:
            width, height = w, h
        elif (w, h) != (width, height):
            raise ValueError(f"{source}: dimensions differ from first frame")
        frames[name] = px
    frames["delta-a"] = delta_frame(frames["baseline-a"], frames["effect-a"])
    frames["delta-b"] = delta_frame(frames["baseline-b"], frames["effect-b"])
    frames["prisma-delta-a"] = delta_frame(frames["prisma-baseline-a"], frames["prisma-effect-a"])

    a = metric(frames["baseline-a"], frames["effect-a"], width, height)
    b = metric(frames["baseline-b"], frames["effect-b"], width, height)
    reset = metric(frames["baseline-a"], frames["after-reset-a"], width, height)
    prisma = metric(frames["prisma-baseline-a"], frames["prisma-effect-a"], width, height)
    checks = {
        "effect_visible_in_both_views": a["positive_red_pixels_gt20_inner"] > 40
                                        and b["positive_red_pixels_gt20_inner"] > 40,
        "camera_changes_screen_position": a["positive_red_centroid_x"] is not None
                                        and b["positive_red_centroid_x"] is not None
                                        and b["positive_red_centroid_x"] + 6 < a["positive_red_centroid_x"],
        "opaque_center_depth_test": abs(a["center_red_delta"]) <= 5,
        "reset_restores_baseline_red": reset["maximum_absolute_red_delta"] <= 4,
        "prisma_effect_visible": prisma["positive_red_pixels_gt20_inner"] > 12
                               and prisma["maximum_absolute_red_delta"] > 8,
    }
    sheet_width = 4 * width + 5 * PAD
    sheet_height = 3 * height + 4 * PAD
    canvas = bytearray(bytes(BG) * (sheet_width * sheet_height))
    for row, items in enumerate(LAYOUT):
        for col, name in enumerate(items):
            if name is None:
                continue
            pix = frames[name]
            for y in range(height):
                sx = PAD + col * (width + PAD)
                sy = PAD + row * (height + PAD) + y
                start = (sy * sheet_width + sx) * 3
                canvas[start:start + width * 3] = pix[y * width * 3:(y + 1) * width * 3]
    out = Path(output_prefix)
    out.parent.mkdir(parents=True, exist_ok=True)
    ppm = out.with_suffix(".ppm")
    summary = out.with_suffix(".json")
    write_ppm(ppm, sheet_width, sheet_height, canvas)
    report = {
        "source_prefix": str(prefix),
        "size": [width, height],
        "contact_sheet": str(ppm),
        "contact_sheet_layout": LAYOUT,
        "delta_visualization": "absolute per-channel pixel differences multiplied by 4",
        "metrics": {"camera_a": a, "camera_b": b, "reset": reset, "prisma": prisma},
        "checks": checks,
        "strict_pass": all(checks.values()),
        "note": "PPM pixel metrics; not an evaluation of subjective visual quality, GPU residency or frame timing",
    }
    summary.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"[spectrafx-visual] contact_sheet={ppm} report={summary}")
    print(f"[spectrafx-visual] changed A/B/PRISMA={a['changed_rgb_pixels_gt12']}/"
          f"{b['changed_rgb_pixels_gt12']}/{prisma['changed_rgb_pixels_gt12']}")
    for key, passed in checks.items():
        print(f"[spectrafx-visual] {'PASS' if passed else 'FAIL'} {key}")
    return 0 if all(checks.values()) or not strict else 1


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--prefix", default="/tmp/spectrafx-scene", help="prefix used by Metal PPM capture")
    p.add_argument("--output-prefix", help="write <prefix>.ppm + <prefix>.json")
    p.add_argument("--strict", action="store_true", help="nonzero exit if any objective pixel check fails")
    args = p.parse_args()
    try:
        return analyze(args.prefix, args.output_prefix or args.prefix + "-review", args.strict)
    except (OSError, ValueError) as exc:
        p.exit(1, f"[spectrafx-visual] ERROR {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
