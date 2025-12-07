#include "animation/AnimationClip.h"
#include "animation/Skeleton.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace MiEngine {

// Helper: find keyframe index for interpolation
template<typename T>
static size_t findKeyframeIndex(const std::vector<Keyframe<T>>& keys, float time) {
    if (keys.size() <= 1) return 0;

    for (size_t i = 0; i < keys.size() - 1; ++i) {
        if (time < keys[i + 1].time) {
            return i;
        }
    }
    return keys.size() - 2;
}

// Helper: compute interpolation factor
template<typename T>
static float getInterpolationFactor(const std::vector<Keyframe<T>>& keys, size_t index, float time) {
    if (keys.size() <= 1) return 0.0f;

    float t0 = keys[index].time;
    float t1 = keys[index + 1].time;
    float delta = t1 - t0;

    if (delta <= 0.0f) return 0.0f;

    return std::clamp((time - t0) / delta, 0.0f, 1.0f);
}

// BoneAnimationTrack implementations

glm::vec3 BoneAnimationTrack::samplePosition(float time) const {
    if (positionKeys.empty()) {
        return glm::vec3(0.0f);
    }
    if (positionKeys.size() == 1) {
        return positionKeys[0].value;
    }

    size_t index = findKeyframeIndex(positionKeys, time);
    float factor = getInterpolationFactor(positionKeys, index, time);

    const glm::vec3& start = positionKeys[index].value;
    const glm::vec3& end = positionKeys[index + 1].value;

    return glm::mix(start, end, factor);
}

glm::quat BoneAnimationTrack::sampleRotation(float time) const {
    if (rotationKeys.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (rotationKeys.size() == 1) {
        return rotationKeys[0].value;
    }

    size_t index = findKeyframeIndex(rotationKeys, time);
    float factor = getInterpolationFactor(rotationKeys, index, time);

    const glm::quat& start = rotationKeys[index].value;
    const glm::quat& end = rotationKeys[index + 1].value;

    // Spherical linear interpolation for quaternions
    return glm::slerp(start, end, factor);
}

glm::vec3 BoneAnimationTrack::sampleScale(float time) const {
    if (scaleKeys.empty()) {
        return glm::vec3(1.0f);
    }
    if (scaleKeys.size() == 1) {
        return scaleKeys[0].value;
    }

    size_t index = findKeyframeIndex(scaleKeys, time);
    float factor = getInterpolationFactor(scaleKeys, index, time);

    const glm::vec3& start = scaleKeys[index].value;
    const glm::vec3& end = scaleKeys[index + 1].value;

    return glm::mix(start, end, factor);
}

glm::mat4 BoneAnimationTrack::sampleMatrix(float time) const {
    if (matrixKeys.empty()) {
        return glm::mat4(1.0f);
    }
    if (matrixKeys.size() == 1) {
        return matrixKeys[0].value;
    }

    size_t index = findKeyframeIndex(matrixKeys, time);
    float factor = getInterpolationFactor(matrixKeys, index, time);

    // Linear interpolation of matrices (not ideal but simple)
    // For better results, decompose, interpolate, recompose
    const glm::mat4& start = matrixKeys[index].value;
    const glm::mat4& end = matrixKeys[index + 1].value;

    // Simple lerp of matrix elements
    glm::mat4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result[i][j] = glm::mix(start[i][j], end[i][j], factor);
        }
    }
    return result;
}

glm::mat4 BoneAnimationTrack::sample(float time) const {
    // Use matrix keys if available (for global transforms)
    if (!matrixKeys.empty()) {
        return sampleMatrix(time);
    }

    // Otherwise compose from position/rotation/scale
    glm::vec3 position = samplePosition(time);
    glm::quat rotation = sampleRotation(time);
    glm::vec3 scale = sampleScale(time);

    // Compose: T * R * S
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
    transform = transform * glm::toMat4(rotation);
    transform = glm::scale(transform, scale);

    return transform;
}

// AnimationClip implementations

AnimationClip::AnimationClip(const std::string& name, float duration, float ticksPerSecond)
    : m_name(name)
    , m_duration(duration)
    , m_ticksPerSecond(ticksPerSecond) {
}

BoneAnimationTrack& AnimationClip::addTrack(const std::string& boneName) {
    // Check if track already exists
    for (auto& track : m_tracks) {
        if (track.boneName == boneName) {
            return track;
        }
    }

    m_tracks.emplace_back();
    m_tracks.back().boneName = boneName;
    return m_tracks.back();
}

BoneAnimationTrack* AnimationClip::getTrack(const std::string& boneName) {
    for (auto& track : m_tracks) {
        if (track.boneName == boneName) {
            return &track;
        }
    }
    return nullptr;
}

const BoneAnimationTrack* AnimationClip::getTrack(const std::string& boneName) const {
    for (const auto& track : m_tracks) {
        if (track.boneName == boneName) {
            return &track;
        }
    }
    return nullptr;
}

BoneAnimationTrack* AnimationClip::getTrack(uint32_t boneIndex) {
    for (auto& track : m_tracks) {
        if (track.boneIndex == static_cast<int32_t>(boneIndex)) {
            return &track;
        }
    }
    return nullptr;
}

const BoneAnimationTrack* AnimationClip::getTrack(uint32_t boneIndex) const {
    for (const auto& track : m_tracks) {
        if (track.boneIndex == static_cast<int32_t>(boneIndex)) {
            return &track;
        }
    }
    return nullptr;
}

void AnimationClip::bindToSkeleton(const Skeleton& skeleton) {
    for (auto& track : m_tracks) {
        track.boneIndex = skeleton.getBoneIndex(track.boneName);
    }
}

std::vector<glm::mat4> AnimationClip::sample(float time, uint32_t boneCount, bool loop) const {
    // Handle looping
    float animTime = time;
    if (m_duration > 0.0f) {
        if (loop) {
            animTime = std::fmod(time, m_duration);
            if (animTime < 0.0f) animTime += m_duration;
        } else {
            animTime = std::clamp(time, 0.0f, m_duration);
        }
    }

    // Initialize with identity matrices
    std::vector<glm::mat4> localPoses(boneCount, glm::mat4(1.0f));

    // Sample each track
    for (const auto& track : m_tracks) {
        if (track.boneIndex >= 0 && track.boneIndex < static_cast<int32_t>(boneCount)) {
            localPoses[track.boneIndex] = track.sample(animTime);
        }
    }

    return localPoses;
}

glm::mat4 AnimationClip::sampleBone(uint32_t boneIndex, float time) const {
    const BoneAnimationTrack* track = getTrack(boneIndex);
    if (track) {
        return track->sample(time);
    }
    return glm::mat4(1.0f);
}

} // namespace MiEngine
