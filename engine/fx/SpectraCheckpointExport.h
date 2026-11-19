// SpectraFX Stage 39 — bounded, *diagnostic-only* in-memory GPU checkpoint
// binary. This is NOT an ECS/savegame format or portable GPU serializer.
// Only current-revision, complete, kill-fence-safe captures may be exported.
#pragma once

#include "SpectraGpuCheckpoint.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace aiverse::spectra {

// Native build ABI, little-endian on current supported Metal platforms.
// No struct layout portability or future version migration is promised.
struct GpuCheckpointExportHeader {
    uint32_t magic=0x58465053u; // little-endian "SPFX"
    uint32_t version=1;
    uint32_t headerBytes=sizeof(GpuCheckpointExportHeader);
    uint32_t physicalSlots=0;
    uint64_t capturedRevision=0;
    uint64_t gpuFrameSerial=0;
    uint64_t payloadBytes=0;
};
static_assert(sizeof(GpuCheckpointExportHeader)==40);
static_assert(std::is_trivially_copyable_v<GpuCheckpointExportHeader>);
static_assert(std::is_trivially_copyable_v<GpuCheckpointParticle>);

inline constexpr uint32_t kGpuCheckpointExportMaxSlots=4096;
// Stage 32 raw GPU capture: <=512 KiB; extra CPU lifespan mirror <=16 KiB
// plus 40-byte descriptive header.
inline constexpr size_t kGpuCheckpointExportMaxBytes=
    sizeof(GpuCheckpointExportHeader)+
    size_t(kGpuCheckpointExportMaxSlots)*
    (sizeof(GpuCheckpointParticle)+sizeof(float));

// Must be called only AFTER a caller has checked the current revision,
// GPU health, no unsent births, no pending selective kills/fences, and an
// eligible completed checkpoint. Never read GPU memory in this helper.
inline bool encodeGpuCheckpointDiagnostic(const GpuCheckpoint& capture,
    uint64_t capturedRevision, std::vector<uint8_t>& output,
    size_t maxBytes=kGpuCheckpointExportMaxBytes) {
    const size_t slots=capture.slots.size();
    if(!capture.complete || slots==0 ||
       slots>kGpuCheckpointExportMaxSlots ||
       slots!=capture.remainingLifetime.size())return false;
    const size_t slotBytes=slots*sizeof(GpuCheckpointParticle);
    const size_t lifetimeBytes=slots*sizeof(float);
    const size_t total=sizeof(GpuCheckpointExportHeader)+
                       slotBytes+lifetimeBytes;
    if(total>kGpuCheckpointExportMaxBytes || total>maxBytes ||
       slots>std::numeric_limits<uint32_t>::max())return false;
    GpuCheckpointExportHeader header;
    header.physicalSlots=uint32_t(slots);
    header.capturedRevision=capturedRevision;
    header.gpuFrameSerial=capture.frameSerial;
    header.payloadBytes=slotBytes+lifetimeBytes;
    std::vector<uint8_t> encoded(total);
    std::memcpy(encoded.data(),&header,sizeof(header));
    std::memcpy(encoded.data()+sizeof(header),
                capture.slots.data(),slotBytes);
    std::memcpy(encoded.data()+sizeof(header)+slotBytes,
                capture.remainingLifetime.data(),lifetimeBytes);
    output.swap(encoded); // output unchanged on any validation failure
    return true;
}

} // namespace aiverse::spectra
