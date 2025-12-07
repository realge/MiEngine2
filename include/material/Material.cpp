#include "Material.h"
std::shared_ptr<Texture> Material::createCombinedMetallicRoughnessTexture(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool, 
    VkQueue graphicsQueue,
    std::shared_ptr<Texture> metallicTex, 
    std::shared_ptr<Texture> roughnessTex) {
    
    // If we only have one texture, return that one
    if (!metallicTex && !roughnessTex) {
        return nullptr;
    }
    if (metallicTex && !roughnessTex) {
        return metallicTex;
    }
    if (!metallicTex && roughnessTex) {
        return roughnessTex;
    }
    
    // TODO: In a full implementation, you would read both textures and combine
    // them into a new texture with metallic in B channel and roughness in G channel.
    // For now, we'll just return the metallic texture as a simplification.
    
    return metallicTex;
}