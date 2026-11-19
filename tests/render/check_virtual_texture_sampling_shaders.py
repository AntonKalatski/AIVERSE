#!/usr/bin/env python3
"""Compile the canonical AIVERSE virtual-texture sampling snippets.

This validates actual GLSL 330 and GLSL ES 300 fragment shader source
and, when explicitly requested on macOS, the production Metal MSL helper.
It does not pretend that shader compilation establishes GPU pixel parity.
No mocks: the embedded source is read from VirtualTextureSampling.h.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "engine" / "gfx" / "VirtualTextureSampling.h"


def extract_shader(header: str, symbol: str) -> str:
    expression = re.compile(
        r"\b" + re.escape(symbol) + r'\s*=\s*R"AVT\((.*?)\)AVT";',
        re.DOTALL,
    )
    matches = expression.findall(header)
    if len(matches) != 1:
        raise RuntimeError(
            f"FAIL: expected exactly one source definition of {symbol}, "
            f"found {len(matches)}"
        )
    return matches[0]


def compile_source(command: list[str], label: str) -> None:
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(
            f"FAIL: {label} shader compile exited {result.returncode}\n"
            + result.stdout + result.stderr
        )
    print(f"PASS: canonical VT sampler compiles for {label}")


def test_glsl(source: str) -> None:
    compiler = shutil.which("glslangValidator")
    if not compiler:
        raise RuntimeError(
            "FAIL: glslangValidator is required for GL33/GLES3 shader compilation"
        )
    for profile, version, preamble in (
        ("OpenGL 3.3", "#version 330 core\n", ""),
        (
            "OpenGL ES 3.0",
            "#version 300 es\n",
            "precision highp float;\nprecision highp int;\n",
        ),
    ):
        shader = (
            version + preamble
            + "layout(location = 0) out vec4 fragmentColor;\n"
            + "uniform highp sampler2DArray physicalAtlas;\n"
            + "uniform highp sampler2DArray virtualTable;\n"
            + "uniform vec2 testUV;\n"
            + "uniform int virtualLayer;\n"
            + "uniform float gutterPixels;\n"
            + "uniform int addressU;\n"
            + "uniform int addressV;\n"
            + source + "\n"
            + "void main() {\n"
            + "    vec4 fallback = vec4(0.13,0.31,0.57,1.0);\n"
            + "    AiverseVirtualPage page = aiverseLookupVirtual(\n"
            + "        physicalAtlas, virtualTable, testUV, virtualLayer,\n"
            + "        gutterPixels, addressU, addressV);\n"
            + "    vec4 addressed = aiverseSampleVirtual(\n"
            + "        physicalAtlas, virtualTable, testUV, virtualLayer,\n"
            + "        gutterPixels, fallback, addressU, addressV);\n"
            + "    vec4 legacy = aiverseSampleVirtual(\n"
            + "        physicalAtlas, virtualTable, testUV, virtualLayer,\n"
            + "        gutterPixels, fallback);\n"
            + "    vec2 uvDx = dFdx(testUV);\n"
            + "    vec2 uvDy = dFdy(testUV);\n"
            + "    vec4 explicitGrad = aiverseSampleVirtualGrad(\n"
            + "        physicalAtlas, virtualTable, testUV, virtualLayer,\n"
            + "        gutterPixels, fallback, uvDx, uvDy, addressU, addressV);\n"
            + "    vec4 gradLegacy = aiverseSampleVirtualGrad(\n"
            + "        physicalAtlas, virtualTable, testUV, virtualLayer,\n"
            + "        gutterPixels, fallback, uvDx, uvDy);\n"
            + "    fragmentColor = addressed + legacy * 0.001 +\n"
            + "        (explicitGrad + gradLegacy) * 0.001 +\n"
            + "        (page.valid ? vec4(page.uv,0.0,0.0) * 0.001 : vec4(0.0));\n"
            + "}\n"
        )
        with tempfile.TemporaryDirectory(prefix="aiverse-vt-glsl-") as temp:
            fragment = pathlib.Path(temp) / "vt_sampling.frag"
            fragment.write_text(shader, encoding="utf-8")
            compile_source([compiler, "-S", "frag", str(fragment)], profile)


def test_metal(source: str) -> None:
    if sys.platform != "darwin" or not shutil.which("xcrun"):
        raise RuntimeError(
            "FAIL: macOS xcrun with Metal toolchain is required for native MSL"
        )
    shader = (
        "#include <metal_stdlib>\nusing namespace metal;\n"
        + source
        + "\nstruct VtCheckVertex { float4 position [[position]]; float2 uv; };\n"
        + "vertex VtCheckVertex vt_vs(uint id [[vertex_id]]) {\n"
        + "  float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};\n"
        + "  VtCheckVertex out; out.position=float4(p[id],0,1);\n"
        + "  out.uv=(p[id]+1.0)*0.5; return out;\n"
        + "}\n"
        + "fragment float4 vt_fs(VtCheckVertex in [[stage_in]],\n"
        + "  texture2d_array<float,access::sample> atlas [[texture(0)]],\n"
        + "  texture2d_array<float,access::read> table [[texture(1)]],\n"
        + "  sampler atlasSampler [[sampler(0)]],\n"
        + "  constant float4& params [[buffer(0)]],\n"
        + "  constant uint2& address [[buffer(1)]]) {\n"
        + "  AiverseVirtualPage lookup=aiverse_lookup_virtual(atlas,table,in.uv,\n"
        + "    uint(params.x),params.y,address.x,address.y);\n"
        + "  float4 fallback=float4(.13,.31,.57,1);\n"
        + "  float4 addressed=aiverse_sample_virtual(atlas,table,atlasSampler,\n"
        + "    in.uv,uint(params.x),params.y,fallback,address.x,address.y);\n"
        + "  float4 legacy=aiverse_sample_virtual(atlas,table,atlasSampler,\n"
        + "    in.uv,uint(params.x),params.y,fallback);\n"
        + "  float2 uvDx = dfdx(in.uv);\n"
        + "  float2 uvDy = dfdy(in.uv);\n"
        + "  float4 explicitGrad=aiverse_sample_virtual_grad(atlas,table,atlasSampler,\n"
        + "    in.uv,uint(params.x),params.y,fallback,uvDx,uvDy,address.x,address.y);\n"
        + "  float4 gradLegacy=aiverse_sample_virtual_grad(atlas,table,atlasSampler,\n"
        + "    in.uv,uint(params.x),params.y,fallback,uvDx,uvDy);\n"
        + "  return addressed+legacy*.001f+(explicitGrad+gradLegacy)*.001f+\n"
        + "    (lookup.valid?float4(lookup.uv,0,0)*.001f:float4(0));\n"
        + "}\n"
    )
    with tempfile.TemporaryDirectory(prefix="aiverse-vt-metal-") as temp:
        fragment = pathlib.Path(temp) / "vt_sampling.metal"
        air = pathlib.Path(temp) / "vt_sampling.air"
        fragment.write_text(shader, encoding="utf-8")
        compile_source(
            ["xcrun", "-sdk", "macosx", "metal", "-c", str(fragment),
             "-o", str(air)],
            "Metal macOS",
        )
        if not air.is_file() or air.stat().st_size == 0:
            raise RuntimeError("FAIL: Metal compiler produced no AIR output")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--backend", choices=("glsl", "metal", "all"), required=True,
        help="explicit compiler target; never silently skip missing toolchains",
    )
    args = parser.parse_args()
    try:
        source = HEADER.read_text(encoding="utf-8")
        if args.backend in ("glsl", "all"):
            test_glsl(extract_shader(source, "kVirtualTextureGLSL"))
        if args.backend in ("metal", "all"):
            test_metal(extract_shader(source, "kVirtualTextureMSL"))
        return 0
    except (OSError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
