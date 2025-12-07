#include "animation/Skeleton.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <stdexcept>

namespace MiEngine {

uint32_t Skeleton::addBone(const std::string& name, int32_t parentIndex,
                           const glm::mat4& inverseBindPose,
                           const glm::mat4& localBindPose) {
    if (m_bones.size() >= MAX_BONES) {
        throw std::runtime_error("Skeleton::addBone: Maximum bone count exceeded (" +
                                 std::to_string(MAX_BONES) + ")");
    }

    if (m_boneNameToIndex.find(name) != m_boneNameToIndex.end()) {
        throw std::runtime_error("Skeleton::addBone: Bone '" + name + "' already exists");
    }

    if (parentIndex >= static_cast<int32_t>(m_bones.size())) {
        throw std::runtime_error("Skeleton::addBone: Invalid parent index " +
                                 std::to_string(parentIndex));
    }

    uint32_t index = static_cast<uint32_t>(m_bones.size());

    Bone bone;
    bone.name = name;
    bone.parentIndex = parentIndex;
    bone.inverseBindPose = inverseBindPose;
    bone.localBindPose = localBindPose;

    // Decompose local bind pose for interpolation
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(localBindPose, bone.bindScale, bone.bindRotation,
                   bone.bindPosition, skew, perspective);

    m_bones.push_back(bone);
    m_boneNameToIndex[name] = index;

    return index;
}

int32_t Skeleton::getBoneIndex(const std::string& name) const {
    auto it = m_boneNameToIndex.find(name);
    if (it != m_boneNameToIndex.end()) {
        return static_cast<int32_t>(it->second);
    }
    return -1;
}

bool Skeleton::hasBone(const std::string& name) const {
    return m_boneNameToIndex.find(name) != m_boneNameToIndex.end();
}

std::vector<glm::mat4> Skeleton::computeGlobalPoses(const std::vector<glm::mat4>& localPoses) const {
    std::vector<glm::mat4> globalPoses(m_bones.size(), glm::mat4(1.0f));

    for (size_t i = 0; i < m_bones.size(); ++i) {
        const Bone& bone = m_bones[i];
        const glm::mat4& localPose = (i < localPoses.size()) ? localPoses[i] : bone.localBindPose;

        if (bone.parentIndex >= 0) {
            // Child bone: multiply by parent's global transform
            globalPoses[i] = globalPoses[bone.parentIndex] * localPose;
        } else {
            // Root bone: local pose is global pose
            globalPoses[i] = localPose;
        }
    }

    return globalPoses;
}

std::vector<glm::mat4> Skeleton::computeFinalBoneMatrices(const std::vector<glm::mat4>& globalPoses) const {
    std::vector<glm::mat4> finalMatrices(m_bones.size(), glm::mat4(1.0f));

    for (size_t i = 0; i < m_bones.size(); ++i) {
        const glm::mat4& globalPose = (i < globalPoses.size()) ? globalPoses[i] : glm::mat4(1.0f);
        finalMatrices[i] = globalPose * m_bones[i].inverseBindPose;
    }

    return finalMatrices;
}

std::vector<glm::mat4> Skeleton::computeFinalBoneMatrices(const std::vector<glm::mat4>& localPoses, bool fromLocal) const {
    if (fromLocal) {
        std::vector<glm::mat4> globalPoses = computeGlobalPoses(localPoses);
        return computeFinalBoneMatrices(globalPoses);
    }
    return computeFinalBoneMatrices(localPoses);
}

std::vector<glm::mat4> Skeleton::getBindPoseMatrices() const {
    // In bind pose, final matrices are identity (globalPose * inverseBindPose = I)
    // This is because globalBindPose * inverseBindPose = I
    return std::vector<glm::mat4>(m_bones.size(), glm::mat4(1.0f));
}

} // namespace MiEngine
