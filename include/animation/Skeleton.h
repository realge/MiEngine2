#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace MiEngine {

/**
 * Represents a single bone in a skeleton hierarchy.
 * Bones are stored in a flat array with parent indices for hierarchy traversal.
 */
struct Bone {
    std::string name;
    int32_t parentIndex = -1;           // -1 indicates root bone

    // Bind pose data
    glm::mat4 inverseBindPose = glm::mat4(1.0f);  // Mesh-space to bone-space
    glm::mat4 localBindPose = glm::mat4(1.0f);    // Local transform in bind pose

    // Decomposed local bind pose for interpolation
    glm::vec3 bindPosition = glm::vec3(0.0f);
    glm::quat bindRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 bindScale = glm::vec3(1.0f);
};

/**
 * Skeleton class manages bone hierarchy and provides utilities for
 * computing global bone transforms from local poses.
 *
 * Usage:
 *   auto skeleton = std::make_shared<Skeleton>();
 *   skeleton->addBone("root", -1, inverseBindPose);
 *   skeleton->addBone("spine", 0, inverseBindPose);  // Parent is root (index 0)
 *
 *   // During animation:
 *   std::vector<glm::mat4> localPoses = animator.getLocalPoses();
 *   std::vector<glm::mat4> globalPoses = skeleton->computeGlobalPoses(localPoses);
 *   std::vector<glm::mat4> finalMatrices = skeleton->computeFinalBoneMatrices(globalPoses);
 */
class Skeleton {
public:
    static constexpr uint32_t MAX_BONES = 256;

    Skeleton() = default;
    ~Skeleton() = default;

    // Bone management
    uint32_t addBone(const std::string& name, int32_t parentIndex,
                     const glm::mat4& inverseBindPose,
                     const glm::mat4& localBindPose = glm::mat4(1.0f));

    // Accessors
    const Bone& getBone(uint32_t index) const { return m_bones[index]; }
    Bone& getBone(uint32_t index) { return m_bones[index]; }
    uint32_t getBoneCount() const { return static_cast<uint32_t>(m_bones.size()); }

    int32_t getBoneIndex(const std::string& name) const;
    bool hasBone(const std::string& name) const;

    const std::vector<Bone>& getBones() const { return m_bones; }

    // Transform computation

    /**
     * Computes global (model-space) transforms for all bones from local poses.
     * @param localPoses Local transform for each bone (indexed by bone ID)
     * @return Global transforms for each bone
     */
    std::vector<glm::mat4> computeGlobalPoses(const std::vector<glm::mat4>& localPoses) const;

    /**
     * Computes the final bone matrices ready for GPU skinning.
     * Final matrix = GlobalPose * InverseBindPose
     * @param globalPoses Global transform for each bone
     * @return Final skinning matrices for shader
     */
    std::vector<glm::mat4> computeFinalBoneMatrices(const std::vector<glm::mat4>& globalPoses) const;

    /**
     * Convenience method: computes final matrices from local poses in one call.
     * @param localPoses Local transform for each bone
     * @return Final skinning matrices for shader
     */
    std::vector<glm::mat4> computeFinalBoneMatrices(const std::vector<glm::mat4>& localPoses, bool fromLocal) const;

    // Get bind pose matrices (identity animation)
    std::vector<glm::mat4> getBindPoseMatrices() const;

private:
    std::vector<Bone> m_bones;
    std::unordered_map<std::string, uint32_t> m_boneNameToIndex;
};

} // namespace MiEngine
