#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace MiEngine {

class JsonWriter;
class JsonReader;

// Transform structure with position, rotation (quaternion), and scale
struct MiTransform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity quaternion (w, x, y, z)
    glm::vec3 scale = glm::vec3(1.0f);

    // Default constructor
    MiTransform() = default;

    // Constructor with position only
    explicit MiTransform(const glm::vec3& pos)
        : position(pos), rotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)), scale(glm::vec3(1.0f)) {}

    // Constructor with position and euler rotation (radians)
    MiTransform(const glm::vec3& pos, const glm::vec3& eulerRadians)
        : position(pos), scale(glm::vec3(1.0f)) {
        setEulerAngles(eulerRadians);
    }

    // Constructor with all components
    MiTransform(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}

    // Get transformation matrix
    glm::mat4 getMatrix() const {
        glm::mat4 result = glm::mat4(1.0f);
        result = glm::translate(result, position);
        result = result * glm::toMat4(rotation);
        result = glm::scale(result, scale);
        return result;
    }

    // Set from transformation matrix
    void setFromMatrix(const glm::mat4& matrix) {
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(matrix, scale, rotation, position, skew, perspective);
    }

    // Euler angles (radians) - returns XYZ order
    glm::vec3 getEulerAngles() const {
        return glm::eulerAngles(rotation);
    }

    // Set rotation from euler angles (radians) - XYZ order
    void setEulerAngles(const glm::vec3& eulerRadians) {
        rotation = glm::quat(eulerRadians);
    }

    // Euler angles in degrees
    glm::vec3 getEulerDegrees() const {
        return glm::degrees(getEulerAngles());
    }

    void setEulerDegrees(const glm::vec3& eulerDegrees) {
        setEulerAngles(glm::radians(eulerDegrees));
    }

    // Direction vectors (in world space, assuming no parent transform)
    glm::vec3 getForward() const {
        return rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 getRight() const {
        return rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::vec3 getUp() const {
        return rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Look at a target position
    void lookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f)) {
        if (glm::length(target - position) > 0.0001f) {
            glm::mat4 lookMatrix = glm::lookAt(position, target, up);
            rotation = glm::conjugate(glm::quat_cast(lookMatrix));
        }
    }

    // Transform a point from local to world space
    glm::vec3 transformPoint(const glm::vec3& point) const {
        return glm::vec3(getMatrix() * glm::vec4(point, 1.0f));
    }

    // Transform a direction from local to world space (ignores position)
    glm::vec3 transformDirection(const glm::vec3& direction) const {
        return rotation * direction;
    }

    // Inverse transform a point from world to local space
    glm::vec3 inverseTransformPoint(const glm::vec3& point) const {
        return glm::vec3(glm::inverse(getMatrix()) * glm::vec4(point, 1.0f));
    }

    // Inverse transform a direction from world to local space
    glm::vec3 inverseTransformDirection(const glm::vec3& direction) const {
        return glm::inverse(rotation) * direction;
    }

    // Combine transforms (this * other)
    MiTransform operator*(const MiTransform& other) const {
        MiTransform result;
        result.scale = scale * other.scale;
        result.rotation = rotation * other.rotation;
        result.position = position + rotation * (scale * other.position);
        return result;
    }

    // Interpolate between two transforms
    static MiTransform lerp(const MiTransform& a, const MiTransform& b, float t) {
        MiTransform result;
        result.position = glm::mix(a.position, b.position, t);
        result.rotation = glm::slerp(a.rotation, b.rotation, t);
        result.scale = glm::mix(a.scale, b.scale, t);
        return result;
    }

    // Get inverse transform
    MiTransform inverse() const {
        MiTransform result;
        result.rotation = glm::inverse(rotation);
        result.scale = 1.0f / scale;
        result.position = result.rotation * (-position * result.scale);
        return result;
    }

    // Identity transform
    static MiTransform identity() {
        return MiTransform();
    }

    // Comparison
    bool operator==(const MiTransform& other) const {
        return position == other.position &&
               rotation == other.rotation &&
               scale == other.scale;
    }

    bool operator!=(const MiTransform& other) const {
        return !(*this == other);
    }

    // Serialization
    void serialize(JsonWriter& writer) const;
    void deserialize(const JsonReader& reader);
};

} // namespace MiEngine
