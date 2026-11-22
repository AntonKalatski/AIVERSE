// Compatibility alias for former RTS model-preparation host service.
// The real CPU worker implementation is shared by all project genres.
#pragma once
#include "engine/render/ModelPreparationService.h"
namespace aiverse::game::view_adapter {
using GameModelPreparationService=::aiverse::render::ModelPreparationService;
}
