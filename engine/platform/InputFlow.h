// AIVerse InputFlow — canonical platform host facade.
//
// This API is presentation-only; authoritative ECS consumes validated Commands.
// Existing EnhancedInput* headers and names remain available for older projects.
#pragma once

#include <aiverselib/gameplay/InputFlow.h>
#include "EnhancedInputAdapter.h"
#include "EnhancedInputConfig.h"
#include "EnhancedInputFileIO.h"
#include "EnhancedInputProfileIO.h"
#include "EnhancedInputPresets.h"
#include "EnhancedInputUserProfiles.h"
#include "EnhancedInputUsers.h"

namespace aiverse::platform {
using InputFlowAdapter = EnhancedInputAdapter;
using InputFlowUsers = EnhancedInputUsers;
namespace inputflow_config = enhanced_config;
namespace inputflow_presets = enhanced_presets;
} // namespace aiverse::platform
