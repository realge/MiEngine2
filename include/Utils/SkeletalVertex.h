#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>

namespace MiEngine {

/**
 * Extended vertex format for skeletal meshes.
 * Adds bone indices and weights for GPU skinning.
 *
 * Memory layout (92 bytes per vertex):
 *   Offset 0:  position    (vec3,  12 bytes)
 *   Offset 12: color       (vec3,  12 bytes)
 *   Offset 24: normal      (vec3,  12 bytes)
 *   Offset 36: texCoord    (vec2,   8 bytes)
 *   Offset 44: tangent     (vec4,  16 bytes)
 *   Offset 60: boneIndices (ivec4, 16 bytes)
 *   Offset 76: boneWeights (vec4,  16 bytes)
 *
 * Shader locations:
 *   0: position
 *   1: color
 *   2: normal
 *   3: texCoord
 *   4: tangent
 *   5: boneIndices
 *   6: boneWeights
 */
struct SkeletalVertex {
    glm::vec3 position;         // 3D position
    glm::vec3 color;            // RGB color
    glm::vec3 normal;           // Normal vector
    glm::vec2 texCoord;         // Texture coordinates (UV)
    glm::vec4 tangent;          // Tangent vector (xyz) + handedness (w)
    glm::ivec4 boneIndices;     // Up to 4 bone influences (indices into bone array)
    glm::vec4 boneWeights;      // Skinning weights (must sum to 1.0)

    static constexpr int MAX_BONE_INFLUENCES = 4;

    SkeletalVertex()
        : position(0.0f)
        , color(1.0f)
        , normal(0.0f, 1.0f, 0.0f)
        , texCoord(0.0f)
        , tangent(1.0f, 0.0f, 0.0f, 1.0f)
        , boneIndices(0)
        , boneWeights(0.0f) {
    }

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(SkeletalVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 7> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions{};

        // Position (location 0)
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(SkeletalVertex, position);

        // Color (location 1)
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(SkeletalVertex, color);

        // Normal (location 2)
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(SkeletalVertex, normal);

        // Texture coordinates (location 3)
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(SkeletalVertex, texCoord);

        // Tangent (location 4)
        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(SkeletalVertex, tangent);

        // Bone indices (location 5) - using SINT for signed integers
        attributeDescriptions[5].binding = 0;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SINT;
        attributeDescriptions[5].offset = offsetof(SkeletalVertex, boneIndices);

        // Bone weights (location 6)
        attributeDescriptions[6].binding = 0;
        attributeDescriptions[6].location = 6;
        attributeDescriptions[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[6].offset = offsetof(SkeletalVertex, boneWeights);

        return attributeDescriptions;
    }

    /**
     * Add a bone influence to this vertex.
     * Automatically finds an empty slot and normalizes weights if needed.
     * @param boneIndex Index of the bone in the skeleton
     * @param weight Influence weight (0-1)
     * @return true if influence was added, false if all slots are full
     */
    bool addBoneInfluence(int32_t boneIndex, float weight) {
        if (weight <= 0.0f) return true;  // Ignore zero weights

        // Find an empty slot (weight == 0)
        for (int i = 0; i < MAX_BONE_INFLUENCES; ++i) {
            if (boneWeights[i] == 0.0f) {
                boneIndices[i] = boneIndex;
                boneWeights[i] = weight;
                return true;
            }
        }

        // All slots full - find the smallest weight and replace if new weight is larger
        int minIndex = 0;
        float minWeight = boneWeights[0];
        for (int i = 1; i < MAX_BONE_INFLUENCES; ++i) {
            if (boneWeights[i] < minWeight) {
                minWeight = boneWeights[i];
                minIndex = i;
            }
        }

        if (weight > minWeight) {
            boneIndices[minIndex] = boneIndex;
            boneWeights[minIndex] = weight;
            return true;
        }

        return false;
    }

    /**
     * Normalize bone weights so they sum to 1.0.
     * Call this after all bone influences have been added.
     */
    void normalizeWeights() {
        float total = boneWeights.x + boneWeights.y + boneWeights.z + boneWeights.w;
        if (total > 0.0001f) {
            boneWeights /= total;
        } else {
            // No weights - default to first bone with full weight
            boneIndices = glm::ivec4(0);
            boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }
};

} // namespace MiEngine
