// SpectraFX Stage 41 — offline native SPFX V2 diagnostic inspector.
// Build from AIVERSE root with: c++ -std=c++20 -I. -O2
// tools/spectrafx/inspect_spfx.cpp -o inspect_spfx
// The program performs no gfx::Device calls, readbacks or gameplay actions.
#include "engine/fx/SpectraCheckpointInspector.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs=std::filesystem;
namespace spectra=aiverse::spectra;

int main(int argc,char** argv) {
    bool json=false;
    std::string name;
    for(int i=1;i<argc;++i) {
        const std::string argument=argv[i];
        if(argument=="--json" && !json)json=true;
        else if(name.empty() && argument.rfind("--",0)!=0)name=argument;
        else {
            std::cerr<<"usage: inspect_spfx [--json] <checkpoint.spfx>\n";
            return 2;
        }
    }
    if(name.empty()) {
        std::cerr<<"usage: inspect_spfx [--json] <checkpoint.spfx>\n";
        return 2;
    }
    std::error_code ec;
    const uintmax_t fileBytes=fs::file_size(name,ec);
    if(ec) {
        std::cerr<<"SPFX: cannot stat input: "<<ec.message()<<"\n";return 2;
    }
    if(fileBytes>spectra::kGpuCheckpointExportMaxBytes) {
        std::cerr<<"SPFX: file exceeds bounded maximum diagnostic payload\n";
        return 2;
    }
    std::ifstream input(name,std::ios::binary);
    if(!input) {std::cerr<<"SPFX: cannot open input\n";return 2;}
    std::vector<uint8_t> bytes(static_cast<size_t>(fileBytes));
    if(!bytes.empty())
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    if(!input && !bytes.empty()) {
        std::cerr<<"SPFX: input truncated while reading\n";return 2;
    }
    // If the source file grew after file_size, don't silently inspect only
    // a valid prefix. Only EXACTLY bounded file contents are accepted.
    char extra=0;
    if(input.get(extra)) {
        std::cerr<<"SPFX: file grew during inspection\n";return 2;
    }

    const auto report=spectra::inspectGpuCheckpointDiagnostic(bytes);
    const std::string_view status=spectra::gpuDiagnosticErrorString(report.error);
    if(json) {
        std::cout<<"{\n"
                 <<"  \"format\": \"SPFX-V2\",\n"
                 <<"  \"valid\": "<<(report.structurallyValid()?"true":"false")<<",\n"
                 <<"  \"error\": \""<<status<<"\",\n";
        if(report.structurallyValid()) {
            const auto& h=report.header;
            std::cout<<"  \"frameSerial\": "<<h.gpuFrameSerial<<",\n"
                     <<"  \"revision\": "<<h.capturedRevision<<",\n"
                     <<"  \"physicalSlots\": "<<h.physicalSlots<<",\n"
                     <<"  \"liveSlots\": "<<report.liveSlots<<",\n"
                     <<"  \"deadSlots\": "<<report.deadSlots<<",\n"
                     <<"  \"nonFiniteLiveSlots\": "<<report.nonFiniteLiveSlots<<",\n"
                     <<"  \"invalidAgeSlots\": "<<report.invalidAgeSlots<<",\n"
                     <<"  \"invalidCpuLifetimeSlots\": "<<report.invalidCpuLifetimeSlots<<",\n"
                     <<"  \"mismatchedLifeSlots\": "<<report.mismatchedLifeSlots<<",\n"
                     <<"  \"payloadCRC32\": "<<h.payloadCrc32<<",\n"
                     <<"  \"containsLivePositions\": "<<
                        (report.containsLivePositions?"true":"false")<<",\n"
                     <<"  \"boundsMin\": ["<<report.minPosition[0]<<", "
                        <<report.minPosition[1]<<", "<<report.minPosition[2]<<"],\n"
                     <<"  \"boundsMax\": ["<<report.maxPosition[0]<<", "
                        <<report.maxPosition[1]<<", "<<report.maxPosition[2]<<"],\n"
                     <<"  \"semanticWarnings\": "<<
                        (report.hasSemanticWarnings()?"true":"false")<<"\n";
        } else {
            std::cout<<"  \"semanticWarnings\": false\n";
        }
        std::cout<<"}\n";
    } else if(report.structurallyValid()) {
        const auto& h=report.header;
        std::cout<<"SPFX V2: header / payload length / CRC32 valid\n"
                 <<"Revision "<<h.capturedRevision<<", GPU frame "
                 <<h.gpuFrameSerial<<", physical slots "<<h.physicalSlots<<"\n"
                 <<"Live "<<report.liveSlots<<", dead "<<report.deadSlots<<"\n"
                 <<"Nonfinite live "<<report.nonFiniteLiveSlots
                 <<", age anomalies "<<report.invalidAgeSlots
                 <<", lifetime mirror anomalies "<<report.invalidCpuLifetimeSlots
                 <<", life mismatches "<<report.mismatchedLifeSlots<<"\n";
        if(report.containsLivePositions)
            std::cout<<"Position bounds ["<<report.minPosition[0]<<", "
                     <<report.minPosition[1]<<", "<<report.minPosition[2]
                     <<"] to ["<<report.maxPosition[0]<<", "
                     <<report.maxPosition[1]<<", "<<report.maxPosition[2]<<"]\n";
        if(report.hasSemanticWarnings())
            std::cout<<"WARNING: structurally valid data contains unusual particle values\n";
        std::cout<<"Note: this is SAME-BUILD V2 diagnostic data, NOT a savegame or GPU replay\n";
    } else {
        std::cerr<<"SPFX inspection failed: "<<status<<"\n";
    }
    return report.structurallyValid()?0:1;
}
