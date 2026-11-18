#!/usr/bin/env bash
# Texture RHI focused native verification. Run at the AIVERSE repo root or
# by absolute path. This script performs real builds/tests only when invoked;
# its existence is NOT evidence that the texture branch passed validation.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${AIVERSE_TEXTURE_BUILD_DIR:-$repo/build/texture-rhi}"
jobs="${AIVERSE_TEXTURE_JOBS:-4}"
want_editor=0
want_vulkan=0
cmake_flags=()

usage() {
    cat <<'USAGE'
Usage: tools/verify-texture-rhi.sh [--editor] [--vulkan] [--jobs N] [--build-dir PATH] [-DCMAKE_VAR=value ...]
  --editor    Build aiverse_editor after focused CPU tests
  --vulkan    Build aiverse_vulkan_tests and run vulkan_backend (inspect skips)
  --jobs N    Parallel compilation tasks (default 4)
  --build-dir PATH  Independent CMake build directory
Outputs: <build-dir>/verify-logs/{configure,build,ctest,summary}.log/txt
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --editor) want_editor=1; shift ;;
        --vulkan) want_vulkan=1; shift ;;
        --jobs)
            [[ $# -ge 2 ]] || { echo "--jobs requires a value" >&2; exit 2; }
            jobs="$2"; shift 2 ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo "--build-dir requires a path" >&2; exit 2; }
            build="$2"; shift 2 ;;
        -D*) cmake_flags+=("$1"); shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
    esac
done
if ! [[ "$jobs" =~ ^[0-9]+$ ]] || [[ "$jobs" -lt 1 ]]; then
    echo "--jobs must be a positive integer" >&2
    exit 2
fi

mkdir -p "$build/verify-logs"
logs="$build/verify-logs"
: >"$logs/build.log"
: >"$logs/ctest.log"
summary="$logs/summary.txt"
{
    printf 'AIVERSE Texture RHI verification\n'
    printf 'UTC %s\n' "$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
    printf 'Repository: %s\n' "$repo"
    printf 'Build directory: %s\n' "$build"
    if command -v git >/dev/null 2>&1; then
        printf 'Commit: %s\n' "$(git -C "$repo" rev-parse HEAD 2>/dev/null || echo unknown)"
        printf 'Branch: %s\n' "$(git -C "$repo" branch --show-current 2>/dev/null || echo unknown)"
    fi
} >"$summary"

echo "Configuring CMake... (see $logs/configure.log)"
if cmake -S "$repo" -B "$build" -DAIVERSE_BUILD_TESTS=ON "${cmake_flags[@]}" \
    >"$logs/configure.log" 2>&1; then
    echo "CONFIGURE PASS" | tee -a "$summary"
else
    echo "CONFIGURE FAIL (see $logs/configure.log)" | tee -a "$summary"
    exit 1
fi

targets=(
    aiverse_texture_contract_test
    aiverse_render_target_set_test
    aiverse_texture_streaming_test
    aiverse_texture_mip_residency_test
    aiverse_texture_resident_streamer_test
    aiverse_virtual_texture_pages_test
    aiverse_virtual_texture_page_table_test
    aiverse_virtual_texture_streamer_test
    aiverse_virtual_texture_disk_queue_test
    aiverse_virtual_texture_file_streamer_test
    aiverse_ktx2_layers_test
    aiverse_ktx2_transcode_test
    aiverse_dds_layers_test
    aiverse_texture_asset_test
    aiverse_hdr_texture_test
    aiverse_exr_texture_test
)
if [[ "$want_editor" -eq 1 ]]; then targets+=(aiverse_editor); fi
if [[ "$want_vulkan" -eq 1 ]]; then targets+=(aiverse_vulkan_tests); fi

failures=0
for target in "${targets[@]}"; do
    echo "BUILD $target"
    {
        printf '\n===== %s =====\n' "$target"
        cmake --build "$build" --target "$target" --parallel "$jobs"
    } >>"$logs/build.log" 2>&1
    status=$?
    if [[ "$status" -eq 0 ]]; then
        echo "BUILD PASS $target" | tee -a "$summary"
    else
        echo "BUILD FAIL $target (exit $status)" | tee -a "$summary"
        failures=$((failures+1))
    fi
done

test_filter='^(texture_contract|render_target_set|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|virtual_texture_disk_queue|virtual_texture_file_streamer|virtual_texture_pack|ktx2_layers|ktx2_transcode|dds_layers|texture_asset|hdr_texture|exr_texture)$'
if [[ "$want_vulkan" -eq 1 ]]; then
    test_filter='^(texture_contract|render_target_set|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|virtual_texture_disk_queue|virtual_texture_file_streamer|virtual_texture_pack|ktx2_layers|ktx2_transcode|dds_layers|texture_asset|hdr_texture|exr_texture|vulkan_backend)$'
fi

echo "Running focused CTest... (see $logs/ctest.log)"
ctest --test-dir "$build" --output-on-failure -R "$test_filter" \
    >"$logs/ctest.log" 2>&1
ctest_status=$?
if [[ "$ctest_status" -eq 0 ]]; then
    echo "CTEST PASS (inspect log for SKIPPED tests and GPU execution)" | tee -a "$summary"
else
    echo "CTEST FAIL (exit $ctest_status)" | tee -a "$summary"
    failures=$((failures+1))
fi

if [[ "$want_vulkan" -eq 1 ]]; then
    echo "VULKAN NOTE: CTest may return success while a missing GPU driver causes a skip." | tee -a "$summary"
    echo "VULKAN NOTE: Read $logs/ctest.log and confirm real Vulkan rendering/readbacks." | tee -a "$summary"
fi
if [[ "$failures" -gt 0 ]]; then
    echo "OVERALL FAIL: $failures build/test gates failed" | tee -a "$summary"
    exit 1
fi
echo "OVERALL SOURCE TESTS PASS (GPU validation remains separate)" | tee -a "$summary"
