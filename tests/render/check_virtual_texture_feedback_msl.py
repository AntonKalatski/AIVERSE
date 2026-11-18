#!/usr/bin/env python3
"""Compile embedded AIVERSE virtual-texture feedback MSL helpers on macOS.

Requires xcrun and the macOS Metal SDK. Missing toolchain is a failure,
not a false shader PASS.
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "engine/gfx/VirtualTextureFeedback.h"


def extract(source, symbol):
    pattern = re.compile(
        r"\b" + re.escape(symbol) + r'\s*=\s*R"VTF\((.*?)\)VTF";',
        re.DOTALL,
    )
    match = pattern.search(source)
    if not match:
        raise RuntimeError(f"missing embedded shader: {symbol}")
    return match.group(1)


def main():
    if sys.platform != "darwin" or not shutil.which("xcrun"):
        print("FAIL: macOS xcrun Metal toolchain required", file=sys.stderr)
        return 2
    source = HEADER.read_text(encoding="utf-8")
    encoder = extract(source, "kVirtualTextureFeedbackMSL")
    material = extract(source, "kVirtualTextureFeedbackMaterialMSL")
    shader = (
        "#include <metal_stdlib>\nusing namespace metal;\n"
        + encoder + "\n" + material + "\n"
        "fragment float4 aiverse_feedback_validation("
        "float2 uv [[user(locn0)]],"
        "constant int &mip [[buffer(0)]],"
        "constant int &layer [[buffer(1)]],"
        "constant int2 &tiles [[buffer(2)]]) {"
        "return aiverse_material_feedback(uv,mip,layer,tiles); }\n"
    )
    with tempfile.TemporaryDirectory(prefix="aiverse-metal-feedback-") as temp:
        src = pathlib.Path(temp) / "feedback.metal"
        output = pathlib.Path(temp) / "feedback.air"
        src.write_text(shader, encoding="utf-8")
        command = ["xcrun", "-sdk", "macosx", "metal", "-c", str(src),
                   "-o", str(output)]
        result = subprocess.run(command, text=True, capture_output=True,
                                check=False)
        if result.returncode:
            print(result.stdout + result.stderr, file=sys.stderr)
            return result.returncode
        if not output.exists():
            print("FAIL: Metal compiler produced no AIR output", file=sys.stderr)
            return 1
    print("PASS: embedded virtual texture MSL feedback shaders compile")
    return 0


if __name__ == "__main__":
    sys.exit(main())
