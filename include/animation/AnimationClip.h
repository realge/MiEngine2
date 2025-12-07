#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <memory>

namespace MiEngine {

/**
 * A single keyframe with time and value.
 */
template<typename T>
struct Keyframe {
    float time = 0.0f;
    T value;

    Keyframe() = default;
    Keyframe(float t, const T& v) : time(t), value(v) {}
};

// Common keyframe types
using PositionKey = Keyframe<glm::vec3>;
using RotationKey = Keyframe<glm::quat>;
using ScaleKey = Keyframe<glm::vec3>;
using MatrixKey = Keyframe<glm::mat4>;

/**
 * Animation data for a single bone.
 * Contains separate tracks for position, rotation, and scale.
 */
struct BoneAnimationTrack {
    std::string boneName;
    int32_t boneIndex = -1;  // Set during skeleton binding

    std::vector<PositionKey> positionKeys;
    std::vector<RotationKey> rotationKeys;
    std::vector<ScaleKey> scaleKeys;
    std::vector<MatrixKey> matrixKeys;  // For storing global transforms directly

    // Sampling methods
    glm::vec3 samplePosition(float time) const;
    glm::quat sampleRotation(float time) const;
    glm::vec3 sampleScale(float time) const;
    glm::mat4 sampleMatrix(float time) const;  // Sample from matrix keys

    // Compose a local transform matrix at given time
    glm::mat4 sample(float time) const;

    bool hasKeys() const {
        return !positionKeys.empty() || !rotationKeys.empty() || !scaleKeys.empty() || !matrixKeys.empty();
    }

    bool hasMatrixKeys() const { return !matrixKeys.empty(); }
};

/**
 * An animation clip containing keyframe data for multiple bones.
 *
 * Usage:
 *   AnimationClip clip;
 *   clip.name = "Walk";
 *   clip.duration = 1.0f;
 *
 *   BoneAnimationTrack& track = clip.addTrack("LeftLeg");
 *   track.positionKeys.push_back({0.0f, glm::vec3(0, 0, 0)});
 *   track.positionKeys.push_back({1.0f, glm::vec3(0, 1, 0)});
 *
 *   // During playback:
 *   std::vector<glm::mat4> localPoses = clip.sample(currentTime);
 */
class AnimationClip {
public:
    AnimationClip() = default;
    AnimationClip(const std::string& name, float duration, float ticksPerSecond = 30.0f);
    ~AnimationClip() = default;

    // Track management
    BoneAnimationTrack& addTrack(const std::string& boneName);
    BoneAnimationTrack* getTrack(const std::string& boneName);
    const BoneAnimationTrack* getTrack(const std::string& boneName) const;
    BoneAnimationTrack* getTrack(uint32_t boneIndex);
    const BoneAnimationTrack* getTrack(uint32_t boneIndex) const;

    /**
     * Bind tracks to skeleton bone indices.
     * Call this after loading to map bone names to indices.
     */
    void bindToSkeleton(const class Skeleton& skeleton);

    /**
     * Sample all bone transforms at the given time.
     * @param time Animation time (will be wrapped if looping)
     * @param boneCount Number of bones in skeleton
     * @param loop Whether to loop the animation
     * @return Local transform matrices for each bone
     */
    std::vector<glm::mat4> sample(float time, uint32_t boneCount, bool loop = true) const;

    /**
     * Sample a single bone's transform at the given time.
     */
    glm::mat4 sampleBone(uint32_t boneIndex, float time) const;

    // Accessors
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    float getDuration() const { return m_duration; }
    void setDuration(float duration) { m_duration = duration; }

    float getTicksPerSecond() const { return m_ticksPerSecond; }
    void setTicksPerSecond(float tps) { m_ticksPerSecond = tps; }

    const std::vector<BoneAnimationTrack>& getTracks() const { return m_tracks; }
    std::vector<BoneAnimationTrack>& getTracks() { return m_tracks; }

    // Flag to indicate if this clip stores global transforms (skip hierarchy computation)
    bool usesGlobalTransforms() const { return m_usesGlobalTransforms; }
    void setUsesGlobalTransforms(bool value) { m_usesGlobalTransforms = value; }

private:
    std::string m_name;
    float m_duration = 0.0f;
    float m_ticksPerSecond = 30.0f;
    bool m_usesGlobalTransforms = false;

    std::vector<BoneAnimationTrack> m_tracks;
};

} // namespace MiEngine
