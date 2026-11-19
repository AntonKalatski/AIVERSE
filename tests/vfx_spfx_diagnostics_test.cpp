// SpectraFX Stage 44 — standalone CPU-only regression for SPFX V2 reports.
// Build: c++ -std=c++20 -O2 -I. tests/vfx_spfx_diagnostics_test.cpp -o /tmp/vfx_spfx_diagnostics_test
#include "engine/fx/SpectraDiagnosticBundle.h"
#include "engine/fx/SpectraCheckpointExport.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace aiverse::spectra;

static uint16_t u16(const std::vector<uint8_t>& b,size_t n) {
    assert(n+2<=b.size());
    return uint16_t(b[n])|uint16_t(b[n+1])<<8;
}
static uint32_t u32(const std::vector<uint8_t>& b,size_t n) {
    return uint32_t(u16(b,n))|uint32_t(u16(b,n+2))<<16;
}
static bool contains(const std::string& s,const std::string& fragment) {
    return s.find(fragment)!=std::string::npos;
}

int main() {
    GpuCheckpoint cp;
    cp.complete=true;cp.frameSerial=9876;
    cp.slots.resize(3);
    cp.remainingLifetime={1.f,-1.f,0.25f};
    cp.slots[0].positionAge={120.f,350.f,900.f,0.25f};
    cp.slots[0].velocityLife={0.f,1.f,0.f,2.f};
    cp.slots[0].tint={1.f,0.5f,1.f,1.f};
    cp.slots[0].size={1.f,0.f,0.f,0.f};
    cp.slots[0].programParams[0]=12345.f; // sensitive VM scalar
    cp.slots[1].positionAge.w=-1.f;
    cp.slots[2].positionAge={-22.f,10.f,8.f,0.75f};
    cp.slots[2].velocityLife={0.f,0.f,0.f,1.f};

    std::vector<uint8_t> encoded;
    assert(encodeGpuCheckpointDiagnostic(cp,7,encoded));
    assert(encoded.size()==sizeof(GpuCheckpointExportHeader)+
          cp.slots.size()*(sizeof(GpuCheckpointParticle)+sizeof(float)));
    assert(validateGpuCheckpointDiagnostic(encoded));

    const auto inspected=inspectGpuCheckpointDiagnostic(encoded);
    assert(inspected.structurallyValid());
    assert(inspected.liveSlots==2 && inspected.deadSlots==1);
    assert(inspected.containsLivePositions);
    assert(inspected.minPosition[0]==-22.f && inspected.maxPosition[0]==120.f);
    assert(inspected.header.capturedRevision==7);
    assert(inspected.header.gpuFrameSerial==9876);

    const std::string safe=gpuDiagnosticReportJson(inspected);
    assert(contains(safe,"spectrafx_sanitized_diagnostic"));
    assert(!contains(safe,"worldBounds"));
    assert(!contains(safe,"gpuFrameSerial"));
    assert(!contains(safe,"capturedRevision"));
    assert(!contains(safe,"payloadCRC32"));
    assert(!contains(safe,"120"));
    assert(!contains(safe,"12345"));
    const std::string withBounds=gpuDiagnosticReportJson(
        inspected,{true,false});
    assert(contains(withBounds,"worldBoundsMin"));
    assert(!contains(withBounds,"gpuFrameSerial"));
    const std::string withIds=gpuDiagnosticReportJson(
        inspected,{false,true});
    assert(contains(withIds,"gpuFrameSerial"));
    assert(!contains(withIds,"worldBoundsMin"));

    auto corrupted=encoded;
    corrupted.back()^=0x13;
    assert(inspectGpuCheckpointDiagnostic(corrupted).error==
           GpuDiagnosticError::CrcMismatch);
    corrupted=encoded;
    corrupted[4]=1; // V1 unsupported
    assert(inspectGpuCheckpointDiagnostic(corrupted).error==
           GpuDiagnosticError::UnsupportedVersion);
    corrupted=encoded;
    corrupted.resize(corrupted.size()-1);
    assert(inspectGpuCheckpointDiagnostic(corrupted).error==
           GpuDiagnosticError::TruncatedPayload);
    corrupted=encoded;
    corrupted.push_back(0xee);
    assert(inspectGpuCheckpointDiagnostic(corrupted).error==
           GpuDiagnosticError::TrailingBytes);
    corrupted=encoded;
    corrupted[0]^=0x11;
    assert(inspectGpuCheckpointDiagnostic(corrupted).error==
           GpuDiagnosticError::BadMagic);
    corrupted=encoded;
    corrupted[12]=0xff;corrupted[13]=0xff; // invalid slot count
    assert(inspectGpuCheckpointDiagnostic(corrupted).error==
           GpuDiagnosticError::InvalidSlotCount);
    assert(inspectGpuCheckpointDiagnostic(nullptr,0).error==
           GpuDiagnosticError::TruncatedHeader);

    std::vector<GpuSafeTimelineFrame> timeline;
    timeline.push_back({0,2,1,0,0,0,true,true});
    timeline.push_back({1,3,1,0,0,0,true,true});
    std::vector<uint8_t> zip;
    assert(makeSafeGpuDiagnosticZip(inspected,timeline,zip));
    assert(zip.size()<kGpuSafeBundleMaxBytes);
    assert(u32(zip,0)==0x04034b50u);
    assert(u32(zip,zip.size()-22)==0x06054b50u);
    assert(u16(zip,zip.size()-12)==3); // central directory entry count
    const uint32_t directoryLength=u32(zip,zip.size()-10);
    const uint32_t directoryStart=u32(zip,zip.size()-6);
    assert(directoryStart+directoryLength+22==zip.size());
    size_t pos=directoryStart;
    const char* expected[]{"manifest.json","checkpoint_summary.json","preview_timeline.json"};
    for(const char* name:expected) {
        assert(u32(zip,pos)==0x02014b50u);
        const size_t filenameBytes=u16(zip,pos+28);
        const size_t extraBytes=u16(zip,pos+30);
        const size_t commentBytes=u16(zip,pos+32);
        const size_t local=u32(zip,pos+42);
        assert(u32(zip,local)==0x04034b50u);
        const std::string actual(
            reinterpret_cast<const char*>(zip.data()+pos+46),
            filenameBytes);
        assert(actual==name);
        assert(u16(zip,pos+10)==0); // STORE, no compression
        const uint32_t crc=u32(zip,pos+16);
        const uint32_t length=u32(zip,pos+24);
        const size_t dataOffset=local+30+filenameBytes;
        assert(dataOffset+length<=zip.size());
        assert(gpuCheckpointPayloadCrc32(zip.data()+dataOffset,length)==crc);
        const std::string body(
            reinterpret_cast<const char*>(zip.data()+dataOffset),length);
        assert(!contains(body,"12345"));
        assert(!contains(body,"worldBoundsMin"));
        assert(!contains(body,"gpuFrameSerial"));
        assert(!contains(body,"payloadCRC32"));
        assert(!contains(body,"120.0"));
        pos+=46+filenameBytes+extraBytes+commentBytes;
    }
    assert(pos==directoryStart+directoryLength);

    auto prior=zip;
    std::vector<GpuSafeTimelineFrame> oversized(kGpuSafeBundleMaxFrames+1);
    assert(!makeSafeGpuDiagnosticZip(inspected,oversized,zip));
    assert(zip==prior); // failed builder doesn't mutate output
    corrupted=encoded;
    corrupted[0]^=0xff;
    assert(!makeSafeGpuDiagnosticZip(
        inspectGpuCheckpointDiagnostic(corrupted),timeline,zip));
    assert(zip==prior);

    std::cout<<"SPFX V2 diagnostics, privacy, corruption, ZIP central "
             <<"directory & CRC assertions passed\n";
    return 0;
}
