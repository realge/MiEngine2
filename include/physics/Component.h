#pragma once
#include <cstdint>
#include <limits>

// Component type enum for runtime type identification
enum class ComponentType : uint8_t {
    None = 0,
    RigidBody,
    Collider
};

// Invalid index constant
constexpr uint32_t INVALID_OWNER_INDEX = std::numeric_limits<uint32_t>::max();

// Base class for all components
class Component {
public:
    virtual ~Component() = default;

    // Get the type of this component for runtime identification
    virtual ComponentType getType() const = 0;

    // Index of owning MeshInstance in Scene's meshInstances vector
    // Use INVALID_OWNER_INDEX if not attached
    uint32_t ownerIndex = INVALID_OWNER_INDEX;

    // Check if component has valid owner
    bool hasOwner() const { return ownerIndex != INVALID_OWNER_INDEX; }

    // Enable/disable component without removing it
    bool enabled = true;
};
