#pragma once

#include "core/MiObject.h"

namespace MiEngine {

class MiActor;
class JsonWriter;
class JsonReader;

// Base class for all components (similar to UActorComponent in UE5)
// Components are modular pieces of functionality that can be attached to actors
class MiComponent : public MiObject {
    MI_OBJECT_BODY(MiComponent, 200)

public:
    MiComponent();
    virtual ~MiComponent() = default;

    // Owner actor
    MiActor* getOwner() const { return m_Owner; }

    // Note: setOwner is called internally by MiActor when adding/removing components
    void setOwner(MiActor* owner) { m_Owner = owner; }

    // Enable/disable component
    bool isEnabled() const { return m_Enabled; }
    void setEnabled(bool enabled);

    // Check if component should tick
    virtual bool isTickable() const { return false; }

    // Get tick priority (lower = earlier, default = 0)
    virtual int getTickPriority() const { return 0; }

    // Lifecycle callbacks (called by MiActor)
    virtual void onAttached() {}     // Called when component is added to an actor
    virtual void onDetached() {}     // Called when component is removed from an actor
    virtual void onRegister() {}     // Called when owner actor is registered to world (good time to load assets)
    virtual void onUnregister() {}   // Called when owner actor is unregistered from world
    virtual void beginPlay() {}      // Called when the game/simulation starts
    virtual void endPlay() {}        // Called when the game/simulation ends
    virtual void tick(float deltaTime) {}  // Called every frame if isTickable() returns true

    // Called when the owner actor's transform changes
    virtual void onOwnerTransformChanged() {}

    // Serialization
    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

protected:
    // Called when enabled state changes
    virtual void onEnabledChanged(bool enabled) {}

    // Get typed owner (convenience method)
    template<typename T>
    T* getOwnerAs() const {
        return dynamic_cast<T*>(m_Owner);
    }

private:
    MiActor* m_Owner = nullptr;
    bool m_Enabled = true;
};

} // namespace MiEngine
