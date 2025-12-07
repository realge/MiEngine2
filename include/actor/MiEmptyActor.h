#pragma once

#include "core/MiActor.h"

namespace MiEngine {

// Empty actor - used for organization and grouping
// Similar to an empty GameObject in Unity or an empty Actor in Unreal
class MiEmptyActor : public MiActor {
    MI_OBJECT_BODY(MiEmptyActor, 105)

public:
    MiEmptyActor();
    virtual ~MiEmptyActor() = default;

    // No additional functionality - just a transform holder
    // Useful for:
    // - Grouping other actors visually in the hierarchy
    // - Acting as a transform parent for child components
    // - Placeholder for spawning other actors at runtime
    // - Waypoints and markers
};

} // namespace MiEngine
