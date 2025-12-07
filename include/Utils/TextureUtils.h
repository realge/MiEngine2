#pragma once
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <vulkan/vulkan.h>
#include "texture/Texture.h"


// Structure to hold cached cubemap data
struct CubemapData {
    std::vector<float> data;  // RGBA float data for all 6 faces
    uint32_t faceSize;        // Width/height of each face
    uint32_t mipLevels;       // Number of mip levels
    
    // Sample cubemap at given direction with bilinear filtering
    glm::vec3 sample(const glm::vec3& direction, uint32_t mipLevel = 0) const;
    
    // Get pointer to specific face data
    const float* getFaceData(uint32_t face, uint32_t mipLevel = 0) const;
    
    // Calculate offset for a specific mip level and face
    size_t getOffset(uint32_t face, uint32_t mipLevel = 0) const;
};

// Cache for environment maps (add as static member or in anonymous namespace)
static std::unordered_map<VkImage, std::shared_ptr<CubemapData>> cubemapCache;

void cacheEnvironmentMap(std::shared_ptr<Texture> environmentMap, std::shared_ptr<CubemapData> data);
std::shared_ptr<CubemapData> getCachedEnvironmentData(std::shared_ptr<Texture> environmentMap);

/**
 * Texture utilities specifically for PBR workflow
 */
class TextureUtils {
public:
    /**
     * IBL Quality Presets
     */
    enum class IBLQuality {
        LOW,      // Fast generation, lower quality
        MEDIUM,   // Balanced quality/performance
        HIGH,     // High quality, slower generation
        ULTRA     // Maximum quality, slowest generation
    };

    /**
     * IBL Configuration Structure
     * Centralizes all resolution and sample count settings for IBL textures
     */
    static uint32_t getPrefilterMapSize() {
        return iblConfig.prefilterMapSize; 
    }
    
    struct IBLConfig {

        
        // Resolution settings
        uint32_t environmentMapSize;     // Base environment cubemap resolution
        uint32_t irradianceMapSize;      // Irradiance map resolution (diffuse IBL)
        uint32_t prefilterMapSize;       // Prefiltered map resolution (specular IBL)
        uint32_t brdfLutResolution;      // BRDF LUT resolution
        
        // Mip levels
        uint32_t prefilterMipLevels;     // Number of mip levels for prefiltered map
        
        // Sample counts for convolution
        uint32_t irradianceSampleCount;  // Samples for irradiance convolution
        uint32_t prefilterBaseSamples;   // Base samples for prefilter (increases with roughness)
        uint32_t brdfLutSamples;         // Samples for BRDF LUT generation
        
        // Default constructor with medium quality
        IBLConfig() : IBLConfig(IBLQuality::MEDIUM) {}
        
        // Constructor with quality preset
        explicit IBLConfig(IBLQuality quality) {
            switch (quality) {
            case IBLQuality::LOW:
                    environmentMapSize = 256;
                    irradianceMapSize = 32;
                    prefilterMapSize = 256;  // CRITICAL FIX: Match environment map size for perfect reflections
                    prefilterMipLevels = static_cast<uint32_t>(std::floor(std::log2(256))) + 1;  // 9 mip levels
                    prefilterBaseSamples = 32;
                    brdfLutSamples = 128;
                    brdfLutResolution = 256;
                    break;
                    
                case IBLQuality::MEDIUM:
                    environmentMapSize = 1024;
                    irradianceMapSize = 64;
                    prefilterMapSize = 128;
                    brdfLutResolution = 256;
                    prefilterMipLevels = 5;
                    irradianceSampleCount = 64;
                    prefilterBaseSamples = 32;
                    brdfLutSamples = 256;
                    break;
                    
                case IBLQuality::HIGH:
                    environmentMapSize = 2048;
                    irradianceMapSize = 128;
                    prefilterMapSize = 256;
                    brdfLutResolution = 512;
                    prefilterMipLevels = 6;
                    irradianceSampleCount = 128;
                    prefilterBaseSamples = 64;
                    brdfLutSamples = 512;
                    break;
                    
                case IBLQuality::ULTRA:
                    environmentMapSize = 4096;
                    irradianceMapSize = 256;
                    prefilterMapSize = 512;
                    brdfLutResolution = 1024;
                    prefilterMipLevels = 7;
                    irradianceSampleCount = 256;
                    prefilterBaseSamples = 128;
                    brdfLutSamples = 1024;
                    break;
            }
        }
        
        // Custom constructor for fine-tuning
        IBLConfig(uint32_t envSize, uint32_t irrSize, uint32_t prefSize, uint32_t brdfSize)
            : environmentMapSize(envSize)
            , irradianceMapSize(irrSize)
            , prefilterMapSize(prefSize)
            , brdfLutResolution(brdfSize)
            , prefilterMipLevels(static_cast<uint32_t>(std::floor(std::log2(prefSize))) + 1)
            , irradianceSampleCount(64)
            , prefilterBaseSamples(32)
            , brdfLutSamples(256) {}
    };

    // Static configuration instance (can be modified at runtime)
    static IBLConfig iblConfig;
    
    // Set global IBL quality
    static void setIBLQuality(IBLQuality quality) {
        iblConfig = IBLConfig(quality);
    }
    
    // Set custom IBL configuration
    static void setIBLConfig(const IBLConfig& config) {
        iblConfig = config;
    }
    
    // Get current IBL configuration
    static const IBLConfig& getIBLConfig() {
        return iblConfig;
    }

    /**
     * Create a default normal map (pointing up in tangent space)
     */
    static glm::vec2 integrateBRDF(float NoV, float roughness);

    // Function declarations
    static std::shared_ptr<CubemapData> readCubemapFromGPU(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        std::shared_ptr<Texture> cubemapTexture
    );
    
    bool initWithExistingImage(
        VkImage image, 
        VkDeviceMemory memory,
        VkFormat format,
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        uint32_t layerCount,
        VkImageViewType viewType,
        VkImageLayout initialLayout);
    
    static std::shared_ptr<Texture> createDefaultNormalMap(
        VkDevice device, 
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue);
    
    /**
     * Create a default metallic-roughness map with given values
     * @param metallic Value for metallic channel (0-1)
     * @param roughness Value for roughness channel (0-1)
     */
    static std::shared_ptr<Texture> createDefaultMetallicRoughnessMap(
        VkDevice device, 
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        float metallic = 0.0f,
        float roughness = 0.5f);
    
    /**
     * Create a solid color texture
     */
    static std::shared_ptr<Texture> createSolidColorTexture(
        VkDevice device, 
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        const glm::vec4& color);
    
    /**
     * Combine separate metallic and roughness textures into a single texture
     * (metallic in blue channel, roughness in green channel)
     */
    static std::shared_ptr<Texture> combineMetallicRoughness(
        VkDevice device, 
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        std::shared_ptr<Texture> metallicTexture,
        std::shared_ptr<Texture> roughnessTexture,
        float defaultMetallic = 0.0f,
        float defaultRoughness = 0.5f);
    
    /**
     * Generate a normal map from a height map
     */
    static std::shared_ptr<Texture> generateNormalFromHeight(
        VkDevice device, 
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        std::shared_ptr<Texture> heightMap,
        float strength = 1.0f);
    
    /**
     * Create a cubemap from 6 individual textures
     */
    static std::shared_ptr<Texture> createCubemap(
        VkDevice device, 
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        const std::array<std::string, 6>& facePaths);
    
    /**
     * Create a BRDF look-up texture for PBR lighting
     * @param resolution Optional resolution override (0 = use config)
     */
    static std::shared_ptr<Texture> createBRDFLookUpTexture(
        VkDevice device, 
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        uint32_t resolution = 0);  // 0 means use config

    /**
     * Create an environment cubemap from an HDR file
     * @param customConfig Optional custom configuration for this specific cubemap
     */
    static std::shared_ptr<Texture> createEnvironmentCubemap(
        VkDevice device, 
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool, 
        VkQueue graphicsQueue,
        const std::string& hdrFilePath,
        const IBLConfig* customConfig = nullptr);
        
    /**
     * Creates a fallback environment cubemap when no HDR file is available
     */
    static std::shared_ptr<Texture> createDefaultEnvironmentCubemap(
        VkDevice device, 
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool, 
        VkQueue graphicsQueue);

    /**
     * Create an irradiance map from an environment map for diffuse IBL
     * @param cacheKey
     * @param customConfig Optional custom configuration for this specific map
     */
    static std::shared_ptr<Texture> createIrradianceMap(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        std::shared_ptr<Texture> environmentMap,
        const std::string& cacheKey, const IBLConfig* customConfig = nullptr);

    
    void static cacheEnvironmentMap(std::shared_ptr<Texture> environmentMap, std::shared_ptr<CubemapData> data);
    std::shared_ptr<CubemapData> getCachedEnvironmentData(std::shared_ptr<Texture> environmentMap);
    void static setCurrentEnvironmentData(std::shared_ptr<CubemapData> data);
    
    /**
     * Create a prefiltered environment map for specular IBL
     * @param cacheKey
     * @param customConfig Optional custom configuration for this specific map
     */
    static std::shared_ptr<Texture> createPrefilterMap(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        std::shared_ptr<Texture> environmentMap,
        const std::string& cacheKey, const IBLConfig* customConfig = nullptr);

private:
    // Helper functions for IBL
    static void equirectangularToCubemapFace(
        float* equirectangularData, int equiWidth, int equiHeight, int channels,
        float* faceData, int faceSize, int faceIndex);
        
    static glm::vec3 specularConvolution(
        std::shared_ptr<Texture> envMap, 
        const glm::vec3& reflection, 
        float roughness, 
        int sampleCount);
        
    static glm::vec3 diffuseConvolution(
        std::shared_ptr<Texture> envMap, 
        const glm::vec3& normal, 
        int sampleCount);
        
    static float distributionGGX(float NoH, float alphaSquared);
    
    static std::vector<glm::vec3> generateHemisphereSamples(
        const glm::vec3& normal, 
        int sampleCount);
        
    static std::vector<glm::vec3> generateImportanceSamples(
        const glm::vec3& reflection, 
        float roughness, 
        int sampleCount);
        
    static glm::vec3 sampleEnvironmentMap(
        std::shared_ptr<Texture> envMap, 
        const glm::vec3& direction);
        
    // Utility functions for Vulkan operations
    static void createBuffer(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory);
        
    static void copyBufferToImage(
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VkBuffer buffer,
        VkImage image,
        uint32_t width,
        uint32_t height,
        uint32_t baseArrayLayer = 0,
        uint32_t mipLevel = 0);
        
    static void transitionImageLayout(
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VkImage image,
        VkFormat format,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t baseArrayLayer = 0,
        uint32_t layerCount = 1,
        uint32_t baseMipLevel = 0,
        uint32_t levelCount = 1);
        
    static uint32_t findMemoryType(
        VkPhysicalDevice physicalDevice,
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties);
};