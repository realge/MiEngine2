#include "core/MiComponent.h"
#include "core/JsonIO.h"
#include "core/MiTypeRegistry.h"

namespace MiEngine {

MiComponent::MiComponent()
    : MiObject()
    , m_Owner(nullptr)
    , m_Enabled(true)
{
    setName("Component");
}

void MiComponent::setEnabled(bool enabled) {
    if (m_Enabled != enabled) {
        m_Enabled = enabled;
        onEnabledChanged(enabled);
        markDirty();
    }
}

void MiComponent::serialize(JsonWriter& writer) const {
    // Call base class serialization
    MiObject::serialize(writer);

    // Component-specific data
    writer.writeBool("enabled", m_Enabled);
}

void MiComponent::deserialize(const JsonReader& reader) {
    // Call base class deserialization
    MiObject::deserialize(reader);

    // Component-specific data
    m_Enabled = reader.getBool("enabled", true);
}

// Register the base component type
MI_REGISTER_TYPE(MiComponent)

} // namespace MiEngine
