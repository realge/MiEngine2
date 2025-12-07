#include "animation/SkeletalMeshComponent.h"
#include "animation/Skeleton.h"
#include "animation/AnimationClip.h"
#include <iostream>

namespace MiEngine {

SkeletalMeshComponent::SkeletalMeshComponent(std::shared_ptr<Skeleton> skeleton)
    : m_skeleton(skeleton) {
    if (m_skeleton) {
        uint32_t boneCount = m_skeleton->getBoneCount();
        m_localPoses.resize(boneCount, glm::mat4(1.0f));
        m_globalPoses.resize(boneCount, glm::mat4(1.0f));
        m_finalBoneMatrices.resize(boneCount, glm::mat4(1.0f));
    }
}

void SkeletalMeshComponent::playAnimation(std::shared_ptr<AnimationClip> clip, bool loop) {
    m_currentClip = clip;
    m_currentTime = 0.0f;
    m_looping = loop;
    m_playing = true;
    m_dirty = true;
}

void SkeletalMeshComponent::stopAnimation() {
    m_playing = false;
    m_currentTime = 0.0f;
    m_dirty = true;
}

void SkeletalMeshComponent::pauseAnimation() {
    m_playing = false;
}

void SkeletalMeshComponent::resumeAnimation() {
    m_playing = true;
}

void SkeletalMeshComponent::update(float deltaTime) {
    if (!m_skeleton) return;

    if (m_playing && m_currentClip) {
        m_currentTime += deltaTime * m_playbackSpeed;

        float duration = m_currentClip->getDuration();
        if (duration > 0.0f) {
            if (m_looping) {
                // Wrap around
                while (m_currentTime >= duration) {
                    m_currentTime -= duration;
                }
                while (m_currentTime < 0.0f) {
                    m_currentTime += duration;
                }
            } else {
                // Clamp to end
                if (m_currentTime >= duration) {
                    m_currentTime = duration;
                    m_playing = false;
                } else if (m_currentTime < 0.0f) {
                    m_currentTime = 0.0f;
                    m_playing = false;
                }
            }
        }

        m_dirty = true;
    }

    if (m_dirty) {
        recalculateBoneMatrices();
        m_dirty = false;
    }
}

void SkeletalMeshComponent::setLocalPose(const std::vector<glm::mat4>& localPoses) {
    m_localPoses = localPoses;

    // Ensure correct size
    if (m_skeleton && m_localPoses.size() < m_skeleton->getBoneCount()) {
        m_localPoses.resize(m_skeleton->getBoneCount(), glm::mat4(1.0f));
    }

    m_dirty = true;
}

void SkeletalMeshComponent::recalculateBoneMatrices() {
    if (!m_skeleton) return;

    uint32_t boneCount = m_skeleton->getBoneCount();

    // Sample animation if playing
    if (m_currentClip && m_playing) {
        // Sample transforms from animation
        m_globalPoses = m_currentClip->sample(m_currentTime, boneCount, m_looping);

        // If clip uses global transforms, use them directly (skip hierarchy computation)
        // Otherwise, treat as local poses and compute hierarchy
        if (!m_currentClip->usesGlobalTransforms()) {
            m_localPoses = m_globalPoses;
            m_globalPoses = m_skeleton->computeGlobalPoses(m_localPoses);
        }

        // Compute final bone matrices (global * inverseBindPose)
        m_finalBoneMatrices = m_skeleton->computeFinalBoneMatrices(m_globalPoses);
    } else {
        // Not playing - use identity matrices (bind pose = no deformation)
        m_finalBoneMatrices.resize(boneCount);
        for (uint32_t i = 0; i < boneCount; ++i) {
            m_finalBoneMatrices[i] = glm::mat4(1.0f);
        }
    }
}

uint32_t SkeletalMeshComponent::getBoneCount() const {
    return m_skeleton ? m_skeleton->getBoneCount() : 0;
}

} // namespace MiEngine
