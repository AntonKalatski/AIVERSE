#!/usr/bin/env python3
"""Compile the actual embedded virtual-texture feedback GLSL snippets.

Requires glslangValidator in PATH. Never silently treats a missing compiler
as a passing shader test. The GLSL snippets are read from the source header.
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "engine/gfx/VirtualTextureFeedback.h"


def snippet(source: str, symbol: str) -> str:
    pattern = re.compile(
        r"\b" + re.escape(symbol) + r'\s*=\s*R"VTF\((.*?)\)VTF";',
        re.DOTALL,
    )
    match = pattern.search(source)
    if match is None:
        raise RuntimeError(f"missing embedded shader: {symbol}")
    return match.group(1)


def main() -> int:
    compiler = shutil.which("glslangValidator")
    if not compiler:
        print("FAIL: glslangValidator is required; shader validation NOT RUN", file=sys.stderr)
        return 2
    source = HEADER.read_text(encoding="utf-8")
    encoder = snippet(source, "kVirtualTextureFeedbackGLSL")
    material = snippet(source, "kVirtualTextureFeedbackMaterialGLSL")
    for version, prefix in (
        ("330 core", ""),
        ("300 es", "precision highp float;\nprecision highp int;\n"),
    ):
        fragment = (
            f"#version {version}\n{prefix}"
            "layout(location=0) out vec4 outFeedback;\n"
            "uniform vec2 uUV;\nuniform int uMip;\n"
            "uniform int uLayer;\nuniform ivec2 uTiles;\n"
            + encoder + "\n" + material + "\n"
            "void main() { outFeedback = aiverseMaterialFeedback("
            "uUV,uMip,uLayer,uTiles); }\n"
        )
        with tempfile.TemporaryDirectory(prefix="aiverse-feedback-") as temp:
            path = pathlib.Path(temp) / "feedback.frag"
            path.write_text(fragment, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-S", "frag", str(path)],
                text=True, capture_output=True, check=False,
            )
            if result.returncode:
                print(f"FAIL GLSL {version}:\n{result.stdout}{result.stderr}", file=sys.stderr)
                return result.returncode
            print(f"PASS GLSL {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
