#include "core/MiTransform.h"
#include "core/JsonIO.h"

namespace MiEngine {

void MiTransform::serialize(JsonWriter& writer) const {
    writer.writeVec3("position", position);
    writer.writeQuat("rotation", rotation);
    writer.writeVec3("scale", scale);
}

void MiTransform::deserialize(const JsonReader& reader) {
    position = reader.getVec3("position", glm::vec3(0.0f));
    rotation = reader.getQuat("rotation", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    scale = reader.getVec3("scale", glm::vec3(1.0f));
}

} // namespace MiEngine
