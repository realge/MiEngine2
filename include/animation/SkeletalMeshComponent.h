#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>

namespace MiEngine {

class Skeleton;
class AnimationClip;

/**
 * SkeletalMeshComponent manages per-instance animation state.
 * Attach this to a MeshInstance that has skeletal mesh data.
 *
 * Usage:
 *   auto skeletal = std::make_shared<SkeletalMeshComponent>(skeleton);
 *   skeletal->playAnimation(walkClip, true);
 *
 *   // Each frame:
 *   skeletal->update(deltaTime);
 *   const auto& boneMatrices = skeletal->getFinalBoneMatrices();
 *   // Upload boneMatrices to GPU UBO
 */
class SkeletalMeshComponent {
public:
    explicit SkeletalMeshComponent(std::shared_ptr<Skeleton> skeleton);
    ~SkeletalMeshComponent() = default;

    // Animation control
    void playAnimation(std::shared_ptr<AnimationClip> clip, bool loop = true);
    void stopAnimation();
    void pauseAnimation();
    void resumeAnimation();

    // Update animation state (call each frame)
    void update(float deltaTime);

    // Manually set pose (for blending, IK, etc.)
    void setLocalPose(const std::vector<glm::mat4>& localPoses);

    // Accessors
    const std::vector<glm::mat4>& getFinalBoneMatrices() const { return m_finalBoneMatrices; }
    const std::vector<glm::mat4>& getGlobalPoses() const { return m_globalPoses; }

    std::shared_ptr<Skeleton> getSkeleton() const { return m_skeleton; }
    std::shared_ptr<AnimationClip> getCurrentClip() const { return m_currentClip; }

    float getCurrentTime() const { return m_currentTime; }
    void setCurrentTime(float time) { m_currentTime = time; m_dirty = true; }

    float getPlaybackSpeed() const { return m_playbackSpeed; }
    void setPlaybackSpeed(float speed) { m_playbackSpeed = speed; }

    bool isPlaying() const { return m_playing; }
    bool isLooping() const { return m_looping; }
    void setLooping(bool loop) { m_looping = loop; }

    // Force recalculation of bone matrices
    void invalidate() { m_dirty = true; }

    // Get bone count for buffer sizing
    uint32_t getBoneCount() const;

private:
    void recalculateBoneMatrices();

    std::shared_ptr<Skeleton> m_skeleton;
    std::shared_ptr<AnimationClip> m_currentClip;

    // Current animation state
    float m_currentTime = 0.0f;
    float m_playbackSpeed = 1.0f;
    bool m_playing = false;
    bool m_looping = true;
    bool m_dirty = true;

    // Cached pose data
    std::vector<glm::mat4> m_localPoses;
    std::vector<glm::mat4> m_globalPoses;
    std::vector<glm::mat4> m_finalBoneMatrices;  // Ready for GPU upload
};

} // namespace MiEngine
