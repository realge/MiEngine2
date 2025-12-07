#pragma once

// MiEngine Core System Headers
// Include this single header to get all core actor/component functionality

#include "core/MiObject.h"
#include "core/MiTransform.h"
#include "core/MiComponent.h"
#include "core/MiSceneComponent.h"
#include "core/MiActor.h"
#include "core/MiWorld.h"
#include "core/MiDelegate.h"
#include "core/MiTypeRegistry.h"
#include "core/JsonIO.h"

namespace MiEngine {

// Version info
constexpr int MIENGINE_VERSION_MAJOR = 2;
constexpr int MIENGINE_VERSION_MINOR = 0;
constexpr int MIENGINE_VERSION_PATCH = 0;

// Engine version string
inline const char* getEngineVersion() {
    return "2.0.0";
}

// Initialize core systems (call once at startup)
inline void initializeCore() {
    // Type registry is automatically initialized via static initialization
    // This function is a placeholder for future initialization needs
}

// Shutdown core systems (call once at shutdown)
inline void shutdownCore() {
    // Cleanup if needed
}

} // namespace MiEngine
