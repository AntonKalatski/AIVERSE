// One list of weak mobile GPUs for every quality default (shadows, materials, lights, PRISMA, particles).
// Measured: Galaxy A05s (Adreno 610, 2400x1080, GLES 3.2) ran skirmish at 1.6 fps on the Medium tiers
// (shadows ~0.5 s, materials ~0.23 s a frame); entry-level Adreno 6xx and Bifrost G3x/G5x belong here.
#pragma once
#include <string>

namespace aiverse::render {

inline bool isWeakMobileGpu(const std::string& name) {
    for (const char* weak : {"Mali-4", "Mali-T", "Mali-G31", "Mali-G51", "Mali-G52",
                             "Adreno (TM) 3", "Adreno (TM) 4", "Adreno (TM) 50", "Adreno (TM) 51",
                             "Adreno (TM) 60", "Adreno (TM) 610", "Adreno (TM) 612", "Adreno (TM) 613",
                             "PowerVR SGX", "PowerVR Rogue GE", "PowerVR Rogue GX6"})
        if (name.find(weak) != std::string::npos) return true;
    return false;
}

} // namespace aiverse::render
