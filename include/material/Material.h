#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

#include "texture/Texture.h"

#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>


// Enum for texture types
enum class TextureType {
    Diffuse,            // Base color/albedo texture
    Normal,             // Normal map
    Metallic,           // Metallic map
    Roughness,          // Roughness map
    MetallicRoughness,  // Combined metallic-roughness map
    AmbientOcclusion,   // Ambient occlusion map
    Emissive,           // Emissive/glow map
    Height,             // Height/displacement map
    Specular,           // Specular map (for non-PBR workflows)
    Count               // Helper to get the count of texture types
};

// Class representing material properties
class Material {
public:
    Material() :
        diffuseColor(1.0f),
        emissiveColor(0.0f),
        metallic(0.0f),
        roughness(0.5f),
        alpha(1.0f),
        emissiveStrength(0.0f),
        descriptorSet(VK_NULL_HANDLE) {}
    
    // Base material properties
    glm::vec3 diffuseColor;
    glm::vec3 emissiveColor;
    float metallic;
    float roughness;
    float alpha;
    float emissiveStrength;
    
    // Set PBR scalar properties
    void setPBRProperties(float metallic, float roughness) {
        this->metallic = metallic;
        this->roughness = roughness;
    }
    
    // Set a texture of a specific type
    void setTexture(TextureType type, std::shared_ptr<Texture> texture) {
        textures[static_cast<int>(type)] = texture;
    }
    
    // Check if a texture of a specific type exists
    bool hasTexture(TextureType type) const {
        int index = static_cast<int>(type);
        return index < textures.size() && textures[index] != nullptr;
    }
    
    // Get a texture of a specific type
    std::shared_ptr<Texture> getTexture(TextureType type) const {
        int index = static_cast<int>(type);
        if (index < textures.size()) {
            return textures[index];
        }
        return nullptr;
    }
    
    // Get image info for a texture of a specific type (for descriptor sets)
    VkDescriptorImageInfo getTextureImageInfo(TextureType type) const {
        std::shared_ptr<Texture> texture = getTexture(type);
        
        VkDescriptorImageInfo imageInfo{};
        if (texture) {
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = texture->getImageView();
            imageInfo.sampler = texture->getSampler();
        }
        
        return imageInfo;
    }
    
    // Set all PBR textures at once
    void setPBRTextures(
        std::shared_ptr<Texture> albedo,
        std::shared_ptr<Texture> normal,
        std::shared_ptr<Texture> metallicRoughness,
        std::shared_ptr<Texture> roughness,
        std::shared_ptr<Texture> ao,
        std::shared_ptr<Texture> emissive)
    {
        if (albedo) setTexture(TextureType::Diffuse, albedo);
        if (normal) setTexture(TextureType::Normal, normal);
        
        // Handle separate or combined metallic/roughness
        if (metallicRoughness) {
            setTexture(TextureType::MetallicRoughness, metallicRoughness);
        } else {
            if (metallicRoughness) setTexture(TextureType::Metallic, getTexture(TextureType::Metallic));
            if (roughness) setTexture(TextureType::Roughness, roughness);
        }
        
        if (ao) setTexture(TextureType::AmbientOcclusion, ao);
        if (emissive) setTexture(TextureType::Emissive, emissive);
    }
    
    // Set the descriptor set for this material
    void setDescriptorSet(VkDescriptorSet set) {
        descriptorSet = set;
    }
    
    // Get the descriptor set for this material
    VkDescriptorSet getDescriptorSet() const {
        return descriptorSet;
    }

    std::shared_ptr<Texture> createCombinedMetallicRoughnessTexture(VkDevice device, VkPhysicalDevice physicalDevice,
                                                                    VkCommandPool commandPool, VkQueue graphicsQueue,
                                                                    std::shared_ptr<Texture> metallicTex,
                                                                    std::shared_ptr<Texture> roughnessTex);

private:
    // Vector of textures indexed by TextureType
    std::vector<std::shared_ptr<Texture>> textures = std::vector<std::shared_ptr<Texture>>(
        static_cast<size_t>(TextureType::Count), nullptr);
    
    // Descriptor set for this material
    VkDescriptorSet descriptorSet;
};

// Material push constants (for fragment shader)


