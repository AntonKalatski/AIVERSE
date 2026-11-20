#!/usr/bin/env python3
"""Stage 104: validate matched 2k/4k CPU Direct/MDI trials and compute medians.

All times are synchronous CPU wall time. The baseline and MDI are separate
processes, so this is not a per-frame randomized causal experiment.
"""
import csv
import math
import pathlib
import statistics
import sys

REQUIRED = ("frame", "render_cpu_ms", "mdi_submissions", "mdi_commands",
            "shadow_draws", "opaque_draws", "shadow_backend_ms",
            "opaque_backend_ms", "render_mach_thread_cpu_ms")


def percentile(data, q):
    ordered = sorted(data)
    return ordered[max(0, math.ceil(q * len(ordered)) - 1)]


def read(path, items, mode):
    with path.open(newline="") as fp:
        reader = csv.DictReader(fp)
        if not all(k in (reader.fieldnames or []) for k in REQUIRED):
            raise ValueError(f"{path}: missing columns {set(REQUIRED)-set(reader.fieldnames or [])}")
        samples = list(reader)
    if [int(r["frame"]) for r in samples] != list(range(90, 196)):
        raise ValueError(f"{path}: expected 106 frames 90..195")
    measured = samples[25:]  # frames 115..195
    if len(measured) != 81:
        raise ValueError(f"{path}: expected 81 measured frames")
    for row in measured:
        if not all(math.isfinite(float(row[k])) and float(row[k]) >= 0
                   for k in REQUIRED):
            raise ValueError(f"{path}: invalid timing or accounting at frame {row['frame']}")
        mdi = int(row["mdi_submissions"])
        commands = int(row["mdi_commands"])
        if mode == "direct" and (mdi or commands):
            raise ValueError(f"{path}: forced Direct unexpectedly used MDI")
        if mode == "mdi" and (mdi <= 0 or commands != 2 * items):
            raise ValueError(f"{path}: forced MDI expected {2*items} indirect commands and nonzero submissions, got {commands}/{mdi}")
    wall = [float(row["render_cpu_ms"]) for row in measured]
    mach = [float(row["render_mach_thread_cpu_ms"]) for row in measured]
    backend = [float(row["shadow_backend_ms"])+float(row["opaque_backend_ms"])
               for row in measured]
    return {
        "items": items, "mode": mode, "file": path.name, "frames": len(measured),
        "wall_median_ms": statistics.median(wall),
        "wall_p95_ms": percentile(wall, .95),
        "wall_max_ms": max(wall),
        "mach_median_ms": statistics.median(mach),
        "offcpu_median_ms": statistics.median(max(0.0, w-t) for w,t in zip(wall,mach)),
        "backend_median_ms": statistics.median(backend),
        "median_mdi_submissions": statistics.median(int(x["mdi_submissions"]) for x in measured),
        "median_mdi_commands": statistics.median(int(x["mdi_commands"]) for x in measured)
    }


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: batching-stage104-analyze.py CAPTURE_DIR")
    folder = pathlib.Path(sys.argv[1])
    # Repeated, interleaved process-order control: D/M/M/D for each size.
    order = ("direct1", "mdi1", "mdi2", "direct2")
    rows = []
    print("Stage 104: matched process-level Direct vs CPU MDI (CPU wall, not GPU/FPS)")
    for items in (2048, 4096):
        group = []
        for label in order:
            mode = "mdi" if label.startswith("mdi") else "direct"
            entry = read(folder / f"{items}-{label}-frames.csv", items, mode)
            entry["trial"] = label
            group.append(entry)
            rows.append(entry)
            print(f"{items:5d} {label:8} wall={entry['wall_median_ms']:.4f}ms "
                  f"p95={entry['wall_p95_ms']:.4f}ms mach={entry['mach_median_ms']:.4f}ms "
                  f"mdi_commands={entry['median_mdi_commands']:.0f}")
        direct = statistics.median(r["wall_median_ms"] for r in group if r["mode"] == "direct")
        mdi = statistics.median(r["wall_median_ms"] for r in group if r["mode"] == "mdi")
        savings = 100 * (direct - mdi) / direct if direct else float("nan")
        print(f"{items:5d} paired process-median Direct={direct:.4f}ms "
              f"MDI={mdi:.4f}ms change={-savings:+.2f}% "
              f"(negative means lower CPU wall for MDI)")
    summary = folder / "stage104-summary.csv"
    with summary.open("w", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Saved: {summary}")
    print("NOTE: results depend on run order/device state. GPU timing/FPS is unknown.")


if __name__ == "__main__":
    main()
