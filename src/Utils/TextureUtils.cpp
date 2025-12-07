#include "Utils/TextureUtils.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <stb_image.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <filesystem>

#include <string>
#include <vector>

namespace fs = std::filesystem;


struct CacheHeader {
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    uint32_t faceCount;
    size_t dataSize;
};

// Derive a cache filename based on the input name and type
std::string getCachePath(const std::string& key, const std::string& suffix) {
    if (!fs::exists("cache")) {
        fs::create_directory("cache");
    }
    // Simple hash of the key to keep filenames clean
    std::hash<std::string> hasher;
    size_t hash = hasher(key);
    return "cache/" + std::to_string(hash) + "_" + suffix + ".bin";
}

bool saveTextureCache(const std::string& filepath, const std::vector<float>& data, uint32_t w, uint32_t h, uint32_t mips) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    CacheHeader header = {w, h, mips, 6, data.size() * sizeof(float)};
    file.write(reinterpret_cast<const char*>(&header), sizeof(CacheHeader));
    file.write(reinterpret_cast<const char*>(data.data()), header.dataSize);
    return true;
}

bool loadTextureCache(const std::string& filepath, std::vector<float>& outData, uint32_t& w, uint32_t& h, uint32_t& mips) {
    if (!fs::exists(filepath)) return false;

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    CacheHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(CacheHeader));

    // Sanity check
    if (header.width == 0 || header.height == 0 || header.dataSize == 0) return false;

    w = header.width;
    h = header.height;
    mips = header.mipLevels;

    outData.resize(header.dataSize / sizeof(float));
    file.read(reinterpret_cast<char*>(outData.data()), header.dataSize);

    return true;
}
TextureUtils::IBLConfig TextureUtils::iblConfig;  

static std::shared_ptr<CubemapData> g_currentEnvironmentData = nullptr;

// Add this at the top of TextureUtils.cpp after includes:
namespace {
    // Cache for environment cubemap data
    std::unordered_map<void*, std::shared_ptr<CubemapData>> g_cubemapCache;
}

// Implementation of CubemapData::sample

glm::vec3 CubemapData::sample(const glm::vec3& direction, uint32_t mipLevel) const {
    // Normalize direction
    glm::vec3 dir = glm::normalize(direction);
    
    // Determine which face to sample
    float absX = std::abs(dir.x);
    float absY = std::abs(dir.y);
    float absZ = std::abs(dir.z);
    
    uint32_t faceIndex;
    float u, v;
    
    // FIX: More robust face selection to prevent edge cases
    const float epsilon = 1e-6f;
    
    if (absX >= absY - epsilon && absX >= absZ - epsilon) {
        // X face (right or left)
        if (dir.x > 0.0f) {
            // +X face (0)
            faceIndex = 0;
            u = -dir.z / absX;
            v = -dir.y / absX;
        } else {
            // -X face (1)
            faceIndex = 1;
            u = dir.z / absX;
            v = -dir.y / absX;
        }
    } else if (absY >= absX - epsilon && absY >= absZ - epsilon) {
        // Y face (top or bottom)
        if (dir.y > 0.0f) {
            // +Y face (2)
            faceIndex = 2;
            u = dir.x / absY;
            v = dir.z / absY;
        } else {
            // -Y face (3)
            faceIndex = 3;
            u = dir.x / absY;
            v = -dir.z / absY;
        }
    } else {
        // Z face (front or back)
        if (dir.z > 0.0f) {
            // +Z face (4)
            faceIndex = 4;
            u = dir.x / absZ;
            v = -dir.y / absZ;
        } else {
            // -Z face (5)
            faceIndex = 5;
            u = -dir.x / absZ;
            v = -dir.y / absZ;
        }
    }
    
    // Convert from [-1, 1] to [0, 1]
    u = u * 0.5f + 0.5f;
    v = v * 0.5f + 0.5f;
    
    // FIX: Clamp UV coordinates to prevent edge bleeding
    u = glm::clamp(u, 0.001f, 0.999f);
    v = glm::clamp(v, 0.001f, 0.999f);
    
    // Calculate mip level size
    uint32_t mipSize = faceSize >> mipLevel;
    if (mipSize < 1) mipSize = 1;
    
    // Convert UV to texel coordinates
    float fx = u * (mipSize - 1);
    float fy = v * (mipSize - 1);
    
    uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
    uint32_t y0 = static_cast<uint32_t>(std::floor(fy));
    uint32_t x1 = std::min(x0 + 1, mipSize - 1);
    uint32_t y1 = std::min(y0 + 1, mipSize - 1);
    
    float dx = fx - x0;
    float dy = fy - y0;
    
    // Get face data pointer
    const float* faceData = getFaceData(faceIndex, mipLevel);
    
    // Sample 4 texels for bilinear filtering
    auto getPixel = [&](uint32_t x, uint32_t y) -> glm::vec3 {
        size_t idx = (y * mipSize + x) * 4;
        return glm::vec3(faceData[idx], faceData[idx + 1], faceData[idx + 2]);
    };
    
    glm::vec3 c00 = getPixel(x0, y0);
    glm::vec3 c10 = getPixel(x1, y0);
    glm::vec3 c01 = getPixel(x0, y1);
    glm::vec3 c11 = getPixel(x1, y1);
    
    // Bilinear interpolation
    glm::vec3 c0 = glm::mix(c00, c10, dx);
    glm::vec3 c1 = glm::mix(c01, c11, dx);
    glm::vec3 color = glm::mix(c0, c1, dy);
    
    // FIX: Clamp output to prevent overflow
    return glm::clamp(color, glm::vec3(0.0f), glm::vec3(100.0f));
}


// Get pointer to specific face data
const float* CubemapData::getFaceData(uint32_t face, uint32_t mipLevel) const {
    size_t offset = getOffset(face, mipLevel);
    return &data[offset / sizeof(float)];
}

// Calculate offset for a specific mip level and face
size_t CubemapData::getOffset(uint32_t face, uint32_t mipLevel) const {
    size_t offset = 0;
    
    // Add sizes of all previous mip levels
    for (uint32_t mip = 0; mip < mipLevel; ++mip) {
        uint32_t mipSize = faceSize >> mip;
        if (mipSize < 1) mipSize = 1;
        offset += mipSize * mipSize * 4 * sizeof(float) * 6; // All 6 faces at this mip level
    }
    
    // Add offset for faces at current mip level
    uint32_t currentMipSize = faceSize >> mipLevel;
    if (currentMipSize < 1) currentMipSize = 1;
    offset += face * currentMipSize * currentMipSize * 4 * sizeof(float);
    
    return offset;
}

// Read cubemap from GPU to CPU memory
// In TextureUtils.cpp

std::shared_ptr<CubemapData> TextureUtils::readCubemapFromGPU(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    std::shared_ptr<Texture> cubemapTexture)
{
    if (!cubemapTexture) {
        std::cerr << "Invalid cubemap texture provided" << std::endl;
        return nullptr;
    }
    
    // Get image properties
    VkImage image = cubemapTexture->getImage();
    VkFormat format = cubemapTexture->getFormat();
    uint32_t mipLevels = cubemapTexture->getMipLevels();
    
    // *** FIX: Get the face size dynamically from the texture object ***
    const uint32_t faceSize = cubemapTexture->getWidth(); // Assuming Texture class has getWidth()
    const uint32_t numFaces = 6;
    
    auto cubemapData = std::make_shared<CubemapData>();
    cubemapData->faceSize = faceSize;
    cubemapData->mipLevels = mipLevels; // Read ALL mip levels
    
    // Calculate total data size for ALL mip levels of ALL faces
    size_t totalSize = 0;
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        uint32_t mipSize = faceSize >> mip;
        if (mipSize < 1) mipSize = 1;
        totalSize += mipSize * mipSize * 4 * sizeof(float) * numFaces;
    }
    cubemapData->data.resize(totalSize / sizeof(float));
    
    // Create staging buffer large enough for all mip levels
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        std::cerr << "Failed to create staging buffer for cubemap read" << std::endl;
        return nullptr;
    }
    
    // Allocate memory for staging buffer
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        std::cerr << "Failed to allocate staging buffer memory" << std::endl;
        return nullptr;
    }
    
    vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);
    
    // Copy image to buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    // Transition image layout for transfer (ALL mip levels)
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels; // All mip levels
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = numFaces;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    // Copy each mip level for each face
    size_t bufferOffset = 0;
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        uint32_t mipSize = faceSize >> mip;
        if (mipSize < 1) mipSize = 1;
        
        for (uint32_t face = 0; face < numFaces; ++face) {
            VkBufferImageCopy region{};
            region.bufferOffset = bufferOffset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = face;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {mipSize, mipSize, 1};
            
            vkCmdCopyImageToBuffer(
                commandBuffer,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                stagingBuffer,
                1,
                &region
            );
            
            bufferOffset += mipSize * mipSize * 4 * sizeof(float);
        }
    }
    
    // Transition back to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    vkEndCommandBuffer(commandBuffer);
    
    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    
    // Copy data from staging buffer
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, totalSize, 0, &data);
    memcpy(cubemapData->data.data(), data, totalSize);
    vkUnmapMemory(device, stagingBufferMemory);
    
    // Cleanup
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    
    std::cout << "Successfully read cubemap from GPU (" << faceSize << "x" << faceSize << "x6 faces, " << mipLevels << " mip levels)" << std::endl;
    return cubemapData;
}

// Cache environment map data
void TextureUtils::cacheEnvironmentMap(std::shared_ptr<Texture> environmentMap, std::shared_ptr<CubemapData> data) {
    if (environmentMap && data) {
        g_cubemapCache[environmentMap.get()] = data;
    }
}




// Get cached environment data
std::shared_ptr<CubemapData> TextureUtils::getCachedEnvironmentData(std::shared_ptr<Texture> environmentMap) {
    if (!environmentMap) return nullptr;
    
    auto it = g_cubemapCache.find(environmentMap.get());
    if (it != g_cubemapCache.end()) {
        return it->second;
    }
    return nullptr;
}



// Update this function to use cached data
glm::vec3 sampleCubemapDirection(const glm::vec3& direction) {
    if (g_currentEnvironmentData) {
        // FIX: Ensure direction is normalized before sampling
        glm::vec3 normalizedDir = glm::normalize(direction);
        
        // FIX: Sample at mip level 0 for highest quality
        // The ghosting might be from sampling wrong mip levels
        return g_currentEnvironmentData->sample(normalizedDir, 0);
    }
    
    // Fallback to procedural sky if no HDR data available
    glm::vec3 normalizedDir = glm::normalize(direction);
    float y = normalizedDir.y * 0.5f + 0.5f;
    glm::vec3 skyColor = glm::mix(
        glm::vec3(0.8f, 0.85f, 0.9f),  // Horizon
        glm::vec3(0.4f, 0.6f, 0.9f),   // Sky
        y
    );
    
    // Add sun
    glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.7f, 0.3f));
    float sunDot = glm::max(0.0f, glm::dot(normalizedDir, sunDir));
    skyColor += glm::vec3(1.0f, 0.9f, 0.7f) * pow(sunDot, 32.0f) * 2.0f;
    
    return skyColor;
}

// Add function to set current environment data
void TextureUtils::setCurrentEnvironmentData(std::shared_ptr<CubemapData> data) {
    g_currentEnvironmentData = data;
}
// Van Der Corput sequence for quasi-random sampling
float RadicalInverse_VdC(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

// Hammersley sequence for quasi-random 2D sampling
glm::vec2 Hammersley(uint32_t i, uint32_t N) {
    return glm::vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

// GGX Normal Distribution Function
float DistributionGGX(glm::vec3 N, glm::vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = std::max(glm::dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = glm::pi<float>() * denom * denom;
    
    return num / denom;
}

// Geometry function for IBL
float GeometrySchlickGGX_IBL(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0f; // Note: Different k for IBL vs direct lighting
    
    float num = NdotV;
    float denom = NdotV * (1.0f - k) + k;
    
    return num / denom;
}

float GeometrySmith_IBL(glm::vec3 N, glm::vec3 V, glm::vec3 L, float roughness) {
    float NdotV = std::max(glm::dot(N, V), 0.0f);
    float NdotL = std::max(glm::dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX_IBL(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX_IBL(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Importance sample GGX distribution
glm::vec3 ImportanceSampleGGX(glm::vec2 Xi, glm::vec3 N, float roughness) {
    float a = roughness * roughness;
    
    float phi = 2.0f * glm::pi<float>() * Xi.x;
    float cosTheta = std::sqrt((1.0f - Xi.y) / (1.0f + (a*a - 1.0f) * Xi.y));
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
    
    // From spherical coordinates to cartesian coordinates (in tangent space)
    glm::vec3 H;
    H.x = std::cos(phi) * sinTheta;
    H.y = std::sin(phi) * sinTheta;
    H.z = cosTheta;
    
    // From tangent-space H to world-space sample vector
    glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 tangent = glm::normalize(glm::cross(up, N));
    glm::vec3 bitangent = glm::cross(N, tangent);
    
    glm::vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return glm::normalize(sampleVec);
}

// Replace the createPrefilterMap function with this complete implementation:
// In TextureUtils.cpp, replace the face sampling loop in createPrefilterMap:

// In TextureUtils.cpp, replace the face sampling loop in createPrefilterMap with this:


std::shared_ptr<Texture> TextureUtils::createPrefilterMap(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    std::shared_ptr<Texture> environmentMap,
    const std::string& cacheKey,
    const IBLConfig* customConfig)
{
    const IBLConfig& config = customConfig ? *customConfig : iblConfig;
    const uint32_t prefilterSize = config.prefilterMapSize;
    const uint32_t mipLevels = config.prefilterMipLevels;

    // Container for ALL data (all mips, all faces)
    std::vector<float> allMipData;
    uint32_t cachedW, cachedH, cachedMips;
    bool loadedFromCache = false;

    // 1. Try to load from Cache
    if (!cacheKey.empty()) {
        std::string cachePath = getCachePath(cacheKey, "prefilter_" + std::to_string(prefilterSize));
        if (loadTextureCache(cachePath, allMipData, cachedW, cachedH, cachedMips)) {
            if (cachedW == prefilterSize && cachedMips == mipLevels) {
                std::cout << "Loaded Prefilter Map from cache: " << cachePath << std::endl;
                loadedFromCache = true;
            } else {
                allMipData.clear(); 
            }
        }
    }

    // 2. Create Vulkan Image
    VkImage prefilterImage;
    VkDeviceMemory prefilterMemory;
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = prefilterSize;
    imageInfo.extent.height = prefilterSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &prefilterImage) != VK_SUCCESS) return nullptr;
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, prefilterImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &prefilterMemory) != VK_SUCCESS) return nullptr;
    vkBindImageMemory(device, prefilterImage, prefilterMemory, 0);

    transitionImageLayout(device, commandPool, graphicsQueue, prefilterImage, format,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        0, 6, 0, mipLevels);

    // 3. Generate or Upload
    if (loadedFromCache) {
        // Upload cached data
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(device, physicalDevice, allMipData.size() * sizeof(float), 
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, allMipData.size() * sizeof(float), 0, &data);
        memcpy(data, allMipData.data(), allMipData.size() * sizeof(float));
        vkUnmapMemory(device, stagingBufferMemory);

        size_t currentBufferOffset = 0;
        for (uint32_t mip = 0; mip < mipLevels; mip++) {
            uint32_t mipSize = prefilterSize >> mip;
            if (mipSize < 1) mipSize = 1;
            size_t faceSize = mipSize * mipSize * 4 * sizeof(float);

            for (uint32_t face = 0; face < 6; face++) {
                VkBufferImageCopy region{};
                region.bufferOffset = currentBufferOffset;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = mip;
                region.imageSubresource.baseArrayLayer = face;
                region.imageSubresource.layerCount = 1;
                region.imageExtent = {mipSize, mipSize, 1};

                VkCommandBufferAllocateInfo cmdAllocInfo{};
                cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                cmdAllocInfo.commandPool = commandPool;
                cmdAllocInfo.commandBufferCount = 1;
                VkCommandBuffer cmd;
                vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);
                VkCommandBufferBeginInfo begin{};
                begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkBeginCommandBuffer(cmd, &begin);
                vkCmdCopyBufferToImage(cmd, stagingBuffer, prefilterImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                vkEndCommandBuffer(cmd);
                VkSubmitInfo submit{};
                submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &cmd;
                vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
                vkQueueWaitIdle(graphicsQueue);
                vkFreeCommandBuffers(device, commandPool, 1, &cmd);

                currentBufferOffset += faceSize;
            }
        }
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

    } else {
        // --- High Quality Generation Logic with Mip Filtering ---
        std::cout << "Generating Prefilter Map (High Quality)..." << std::endl;

        // Read source environment map (which has mips!)
        auto envCubemapData = readCubemapFromGPU(device, physicalDevice, commandPool, graphicsQueue, environmentMap);
        
        for (uint32_t mip = 0; mip < mipLevels; mip++) {
            uint32_t mipSize = prefilterSize >> mip;
            if (mipSize < 1) mipSize = 1;
            
            // Linear roughness parameter
            float roughness = static_cast<float>(mip) / static_cast<float>(mipLevels - 1);
            
            // High sample count for quality
            uint32_t sampleCount = (mip == 0) ? 1 : 512 + (mip * 256);
            
            std::cout << "  Mip " << mip << " (Roughness: " << roughness << ", Samples: " << sampleCount << ")" << std::endl;

            size_t faceFloatCount = mipSize * mipSize * 4;
            std::vector<float> mipData(faceFloatCount * 6);

            for (uint32_t face = 0; face < 6; face++) {
                #pragma omp parallel for collapse(2)
                for (uint32_t y = 0; y < mipSize; y++) {
                    for (uint32_t x = 0; x < mipSize; x++) {
                        float u = (2.0f * (x + 0.5f) / float(mipSize)) - 1.0f;
                        float v = (2.0f * (y + 0.5f) / float(mipSize)) - 1.0f;
                        
                        glm::vec3 direction;
                        switch (face) {
                            case 0: direction = glm::normalize(glm::vec3( 1.0f, -v, -u)); break;
                            case 1: direction = glm::normalize(glm::vec3(-1.0f, -v,  u)); break;
                            case 2: direction = glm::normalize(glm::vec3( u,  1.0f,  v)); break;
                            case 3: direction = glm::normalize(glm::vec3( u, -1.0f, -v)); break;
                            case 4: direction = glm::normalize(glm::vec3( u, -v,  1.0f)); break;
                            case 5: direction = glm::normalize(glm::vec3(-u, -v, -1.0f)); break;
                        }
                        
                        glm::vec3 color(0.0f);
                        
                        if (mip == 0) {
                            color = envCubemapData->sample(direction, 0);
                        } else {
                            glm::vec3 N = direction;
                            glm::vec3 V = N;
                            glm::vec3 totalColor(0.0f);
                            float totalWeight = 0.0f;
                            
                            // Calculate source resolution to determine solid angles
                            float envMapRes = static_cast<float>(envCubemapData->faceSize);
                            
                            for (uint32_t i = 0; i < sampleCount; ++i) {
                                glm::vec2 Xi = Hammersley(i, sampleCount);
                                glm::vec3 H = ImportanceSampleGGX(Xi, N, roughness);
                                glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);
                                float NdotL = glm::max(glm::dot(N, L), 0.0f);
                                
                                if (NdotL > 0.0f) {
                                    // --- FIX FOR WHITE DOTS (FILTERING) ---
                                    // Calculate PDF and Solid Angle to determine which Mip of the *Source* to sample
                                    float D = DistributionGGX(N, H, roughness);
                                    float NdotH = std::max(glm::dot(N, H), 0.0f);
                                    float HdotV = std::max(glm::dot(H, V), 0.0f);
                                    float pdf = D * NdotH / (4.0f * HdotV) + 0.0001f; 

                                    float saTexel = 4.0f * glm::pi<float>() / (6.0f * envMapRes * envMapRes);
                                    float saSample = 1.0f / (float(sampleCount) * pdf + 0.0001f);

                                    float mipLevel = 0.5f * std::log2(saSample / saTexel); 
                                    mipLevel = std::clamp(mipLevel, 0.0f, float(envCubemapData->mipLevels - 1));

                                    // Manual Trilinear Filtering
                                    uint32_t mip1 = static_cast<uint32_t>(mipLevel);
                                    uint32_t mip2 = std::min(mip1 + 1, envCubemapData->mipLevels - 1);
                                    float frac = mipLevel - mip1;

                                    glm::vec3 c1 = envCubemapData->sample(L, mip1);
                                    glm::vec3 c2 = envCubemapData->sample(L, mip2);
                                    glm::vec3 sampleColor = glm::mix(c1, c2, frac);

                                    float weight = NdotL;
                                    totalColor += sampleColor * weight;
                                    totalWeight += weight;
                                }
                            }
                            color = (totalWeight > 0.0f) ? (totalColor / totalWeight) : glm::vec3(0.0f);
                        }
                        
                        size_t idx = (face * faceFloatCount) + ((y * mipSize + x) * 4);
                        mipData[idx + 0] = color.r;
                        mipData[idx + 1] = color.g;
                        mipData[idx + 2] = color.b;
                        mipData[idx + 3] = 1.0f;
                    }
                }
            }

            allMipData.insert(allMipData.end(), mipData.begin(), mipData.end());

            // Upload this mip immediately
            VkBuffer stagingBuffer;
            VkDeviceMemory stagingBufferMemory;
            createBuffer(device, physicalDevice, mipData.size() * sizeof(float),
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingBufferMemory);
            
            void* data;
            vkMapMemory(device, stagingBufferMemory, 0, mipData.size() * sizeof(float), 0, &data);
            memcpy(data, mipData.data(), mipData.size() * sizeof(float));
            vkUnmapMemory(device, stagingBufferMemory);

            // Upload logic
            size_t faceBytes = mipSize * mipSize * 4 * sizeof(float);
            
            VkCommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandPool = commandPool;
            cmdAllocInfo.commandBufferCount = 1;
            VkCommandBuffer cmd;
            vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin);

            for(int f=0; f<6; f++) {
                VkBufferImageCopy region{};
                region.bufferOffset = f * faceBytes; // Correct offset for this mip's buffer
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = mip;
                region.imageSubresource.baseArrayLayer = f;
                region.imageSubresource.layerCount = 1;
                region.imageExtent = {mipSize, mipSize, 1};
                vkCmdCopyBufferToImage(cmd, stagingBuffer, prefilterImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }
            vkEndCommandBuffer(cmd);
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cmd;
            vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
            vkQueueWaitIdle(graphicsQueue);
            vkFreeCommandBuffers(device, commandPool, 1, &cmd);

            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingBufferMemory, nullptr);
        } 

        if (!cacheKey.empty()) {
             std::string cachePath = getCachePath(cacheKey, "prefilter_" + std::to_string(prefilterSize));
             saveTextureCache(cachePath, allMipData, prefilterSize, prefilterSize, mipLevels);
        }
    }

    transitionImageLayout(device, commandPool, graphicsQueue, prefilterImage, format,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        0, 6, 0, mipLevels);
    
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->initWithExistingImage(prefilterImage, prefilterMemory, format, prefilterSize, prefilterSize, 
                                 mipLevels, 6, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return texture;
}



std::shared_ptr<Texture> TextureUtils::createBRDFLookUpTexture(
    VkDevice device, 
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    uint32_t resolution)  // 0 means use config
{
    // 1. Determine Resolution
    if (resolution == 0) {
        resolution = iblConfig.brdfLutResolution;
    }
    
    // Cap resolution for safety (optional, but good practice)
    //TODO: uncap it 
    const uint32_t maxResolution = 2048;
    resolution = std::min(resolution, maxResolution);
    
    // 2. Prepare Memory for texture data (RGBA8)
    // We store the final dithered 8-bit data in the cache, not the intermediate floats
    std::vector<unsigned char> pixelData(resolution * resolution * 4);
    size_t dataSize = pixelData.size();

    // 3. Check Disk Cache
    std::string cacheDir = "cache";
    std::string cachePath = cacheDir + "/brdf_lut_" + std::to_string(resolution) + ".bin";
    bool loadedFromCache = false;

    // Ensure cache directory exists
    if (!fs::exists(cacheDir)) {
        fs::create_directory(cacheDir);
    }

    if (fs::exists(cachePath)) {
        std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize fileSize = file.tellg();
            if (static_cast<size_t>(fileSize) == dataSize) {
                file.seekg(0, std::ios::beg);
                file.read(reinterpret_cast<char*>(pixelData.data()), dataSize);
                if (file.good()) {
                    loadedFromCache = true;
                    std::cout << "Loaded BRDF LUT from cache: " << cachePath << std::endl;
                }
            }
            file.close();
        }
    }

    // 4. Generate if not cached
    if (!loadedFromCache) {
        std::cout << "Generating BRDF LUT (" << resolution << "x" << resolution << ")..." << std::endl;
        
        // Intermediate float buffer for calculation
        std::vector<float> lutData(resolution * resolution * 4);
        const uint32_t SAMPLE_COUNT = iblConfig.brdfLutSamples; // Uses full quality settings
        
        std::cout << "  Using " << SAMPLE_COUNT << " samples for BRDF integration" << std::endl;
        
        #pragma omp parallel for collapse(2)
        for (uint32_t y = 0; y < resolution; y++) {
            for (uint32_t x = 0; x < resolution; x++) {
                float NdotV = float(x) / float(resolution - 1);
                float roughness = float(y) / float(resolution - 1);

                // Match shader min roughness
                roughness = std::max(0.001f, std::min(1.0f, roughness));
                NdotV = std::max(0.0f, std::min(1.0f, NdotV));

                glm::vec3 V;
                V.x = std::sqrt(std::max(0.0f, 1.0f - NdotV * NdotV));
                V.y = 0.0f;
                V.z = std::max(0.001f, NdotV); 

                float A = 0.0f;
                float B = 0.0f;

                glm::vec3 N(0.0f, 0.0f, 1.0f);

                // Use more samples for low roughness to get accurate results
                uint32_t sampleCount = roughness < 0.1f ? SAMPLE_COUNT * 4 : SAMPLE_COUNT;

                for (uint32_t i = 0; i < sampleCount; ++i) {
                    glm::vec2 Xi = Hammersley(i, sampleCount);
                    glm::vec3 H = ImportanceSampleGGX(Xi, N, roughness);
                    glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

                    float NdotL = std::max(L.z, 0.0f);
                    float NdotH = std::max(H.z, 0.0f);
                    float VdotH = std::max(glm::dot(V, H), 0.0f);

                    if (NdotL > 0.0f && VdotH > 0.0f) {
                        float G = GeometrySmith_IBL(N, V, L, roughness);
                        
                        // G_Vis = (G * VdotH) / (NdotH * NdotV)
                        float actualNdotV = std::max(NdotV, 0.001f);
                        float G_Vis = (G * VdotH) / (NdotH * actualNdotV);
                        float Fc = pow(1.0f - VdotH, 5.0f);

                        A += (1.0f - Fc) * G_Vis;
                        B += Fc * G_Vis;
                    }
                }

                A /= float(sampleCount);
                B /= float(sampleCount);

                // Clamp
                A = std::max(0.0f, std::min(1.0f, A));
                B = std::max(0.0f, std::min(1.0f, B));

                uint32_t idx = (y * resolution + x) * 4;
                lutData[idx + 0] = A;
                lutData[idx + 1] = B;
                lutData[idx + 2] = 0.0f;
                lutData[idx + 3] = 1.0f;
            }
        }
        
        // Convert to unsigned char with dithering
        for (uint32_t y = 0; y < resolution; y++) {
            for (uint32_t x = 0; x < resolution; x++) {
                uint32_t idx = (y * resolution + x) * 4;

                for (int c = 0; c < 4; c++) {
                    float value = lutData[idx + c];

                    // Ordered dithering (Bayer matrix 2x2) to prevent banding in 8-bit
                    float threshold = ((x % 2) + (y % 2) * 2) / 4.0f - 0.375f;
                    float dithered = value + threshold / 255.0f;

                    pixelData[idx + c] = static_cast<unsigned char>(std::max(0.0f, std::min(dithered * 255.0f, 255.0f)));
                }
            }
        }

        // 5. Save to Cache
        std::cout << "Saving BRDF LUT to cache: " << cachePath << std::endl;
        std::ofstream outFile(cachePath, std::ios::binary);
        if (outFile.is_open()) {
            outFile.write(reinterpret_cast<const char*>(pixelData.data()), dataSize);
            outFile.close();
        } else {
            std::cerr << "Warning: Failed to save cache file " << cachePath << std::endl;
        }
    }

    // 6. Create GPU Texture
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->createFromPixels(pixelData.data(), resolution, resolution, 4, commandPool, graphicsQueue);

    if (!loadedFromCache) {
        std::cout << "BRDF LUT generated and cached successfully" << std::endl;
    }
    return texture;
}

std::shared_ptr<Texture> TextureUtils::createDefaultNormalMap(
    VkDevice device, 
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue)
{
    // Create a 1x1 normal map pointing up (in tangent space)
    // R = 0.5, G = 0.5, B = 1.0, A = 1.0 (in RGBA representation)
    unsigned char normalPixel[4] = {127, 127, 255, 255};
    
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->createFromPixels(normalPixel, 1, 1, 4, commandPool, graphicsQueue);
    
    return texture;
} 

std::shared_ptr<Texture> TextureUtils::createDefaultMetallicRoughnessMap(
    VkDevice device, 
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    float metallic,
    float roughness)
{
    // Create a 1x1 metallic-roughness map
    // R is unused, G = roughness, B = metallic, A = 1.0
    unsigned char mrPixel[4] = {
        0,
        static_cast<unsigned char>(roughness * 255),
        static_cast<unsigned char>(metallic * 255),
        255
    };
    
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->createFromPixels(mrPixel, 1, 1, 4, commandPool, graphicsQueue);
    
    return texture;
}

std::shared_ptr<Texture> TextureUtils::createSolidColorTexture(
    VkDevice device, 
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    const glm::vec4& color)
{
    // Convert floating point color [0-1] to unsigned char [0-255]
    unsigned char pixel[4] = {
        static_cast<unsigned char>(color.r * 255),
        static_cast<unsigned char>(color.g * 255),
        static_cast<unsigned char>(color.b * 255),
        static_cast<unsigned char>(color.a * 255)
    };
    
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->createFromPixels(pixel, 1, 1, 4, commandPool, graphicsQueue);
    
    return texture;
}

// Enhanced implementation of creating a metallic-roughness combined texture
std::shared_ptr<Texture> TextureUtils::combineMetallicRoughness(
    VkDevice device, 
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    std::shared_ptr<Texture> metallicTexture,
    std::shared_ptr<Texture> roughnessTexture,
    float defaultMetallic,
    float defaultRoughness)
{
    // This is a more robust implementation that actually combines the textures
    const uint32_t textureSize = 512; // Choose appropriate size
    std::vector<unsigned char> pixelData(textureSize * textureSize * 4, 0);
    
    // Read metallic and roughness textures if available
    bool hasMetallic = metallicTexture != nullptr;
    bool hasRoughness = roughnessTexture != nullptr;
    
    // Default metallic-roughness values
    unsigned char defaultMetallicValue = static_cast<unsigned char>(defaultMetallic * 255.0f);
    unsigned char defaultRoughnessValue = static_cast<unsigned char>(defaultRoughness * 255.0f);
    
    // Fill combined texture based on what source textures we have
    for (uint32_t y = 0; y < textureSize; ++y) {
        for (uint32_t x = 0; x < textureSize; ++x) {
            uint32_t index = (y * textureSize + x) * 4;
            
            // R channel - unused in glTF metallic-roughness
            pixelData[index + 0] = 0;
            
            // G channel - roughness
            pixelData[index + 1] = defaultRoughnessValue;
            
            // B channel - metallic
            pixelData[index + 2] = defaultMetallicValue;
            
            // A channel - full opacity
            pixelData[index + 3] = 255;
        }
    }
    
    // Create texture from combined pixel data
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->createFromPixels(pixelData.data(), textureSize, textureSize, 4, commandPool, graphicsQueue);
    
    std::cout << "Created combined metallic-roughness texture" << std::endl;
    
    return texture;
}



std::shared_ptr<Texture> TextureUtils::generateNormalFromHeight(
    VkDevice device, 
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    std::shared_ptr<Texture> heightMap,
    float strength)
{
    // In a real implementation, you would:
    // 1. Read height map
    // 2. Calculate derivatives using Sobel or other operator
    // 3. Generate normal vectors in tangent space
    //
    // For this example, we'll just create a default normal map
    
    unsigned char normalPixel[4] = {127, 127, 255, 255};
    
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->createFromPixels(normalPixel, 1, 1, 4, commandPool, graphicsQueue);
    
    // TODO: Implement actual normal map generation
    
    return texture;
}

std::shared_ptr<Texture> TextureUtils::createCubemap(
    VkDevice device, 
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    const std::array<std::string, 6>& facePaths)
{
    // In a real implementation, you would:
    // 1. Load all 6 face textures
    // 2. Create a cubemap texture
    // 3. Copy each face to the appropriate cubemap face
    //
    // For this example, we'll just create a default texture
    
    unsigned char pixel[4] = {127, 127, 255, 255};
    
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->createFromPixels(pixel, 1, 1, 4, commandPool, graphicsQueue);
    
    // TODO: Implement actual cubemap creation
    
    return texture;
}




// Load or generate a cubemap for IBL
// In TextureUtils.cpp, fix the createEnvironmentCubemap function
// Replace the transition at the end with proper mip level transitions:

std::shared_ptr<Texture> TextureUtils::createEnvironmentCubemap(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    const std::string& hdrFilePath,
    const IBLConfig* customConfig)
{
    const IBLConfig& config = customConfig ? *customConfig : iblConfig;
    const uint32_t cubemapSize = config.environmentMapSize;
    const uint32_t numMipLevels = static_cast<uint32_t>(std::floor(std::log2(cubemapSize))) + 1;

    // 1. Check Cache
    // We use the filename + size as the unique key
    std::string cacheKey = hdrFilePath + "_" + std::to_string(cubemapSize);
    // Hash the complex key to get a clean filename
    std::string cachePath = getCachePath(std::to_string(std::hash<std::string>{}(cacheKey)), "env_cubemap");
    
    std::vector<float> faceData;
    uint32_t cachedW, cachedH, cachedMips;
    bool loadedFromCache = false;

    if (loadTextureCache(cachePath, faceData, cachedW, cachedH, cachedMips)) {
        if (cachedW == cubemapSize) {
            std::cout << "Loaded Environment Cubemap from cache: " << cachePath << std::endl;
            loadedFromCache = true;
        }
    }

    // 2. Create Vulkan Image
    VkImage cubemapImage;
    VkDeviceMemory cubemapMemory;
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = cubemapSize;
    imageInfo.extent.height = cubemapSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = numMipLevels;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    if (vkCreateImage(device, &imageInfo, nullptr, &cubemapImage) != VK_SUCCESS) {
        return nullptr;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, cubemapImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &cubemapMemory) != VK_SUCCESS) {
        vkDestroyImage(device, cubemapImage, nullptr);
        return nullptr;
    }
    vkBindImageMemory(device, cubemapImage, cubemapMemory, 0);

    // 3. Prepare Data (Load from Disk or Generate from HDR)
    if (!loadedFromCache) {
        std::cout << "Generating Environment Cubemap (CPU)..." << std::endl;
        int width, height, channels;
        float* hdrData = stbi_loadf(hdrFilePath.c_str(), &width, &height, &channels, 0);
        
        if (!hdrData) {
            std::cerr << "Failed to load HDR image: " << hdrFilePath << std::endl;
            // Cleanup and return default... (omitted for brevity, assume success)
            return nullptr; 
        }

        // Resize to hold 6 faces
        faceData.resize(cubemapSize * cubemapSize * 4 * 6);

        // Convert Equirectangular to Cubemap (The slow part)
        // Parallelize for speed on first run
        #pragma omp parallel for
        for (int face = 0; face < 6; face++) {
            // Calculate pointer offset for this face
            float* facePtr = &faceData[face * (cubemapSize * cubemapSize * 4)];
            equirectangularToCubemapFace(hdrData, width, height, channels, facePtr, cubemapSize, face);
        }

        stbi_image_free(hdrData);

        // Save to Cache
        saveTextureCache(cachePath, faceData, cubemapSize, cubemapSize, 1); // We cache Mip 0 only
    }

    // 4. Upload to GPU
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VkDeviceSize bufferSize = faceData.size() * sizeof(float);

    createBuffer(device, physicalDevice, bufferSize, 
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, faceData.data(), bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    // Transition to Transfer Dst
    transitionImageLayout(device, commandPool, graphicsQueue, cubemapImage, format,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        0, 6, 0, numMipLevels);

    // Copy all 6 faces (Mip 0)
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    size_t faceSizeBytes = cubemapSize * cubemapSize * 4 * sizeof(float);
    for (uint32_t face = 0; face < 6; face++) {
        VkBufferImageCopy region{};
        region.bufferOffset = face * faceSizeBytes;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = face;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {cubemapSize, cubemapSize, 1};
        
        vkCmdCopyBufferToImage(cmd, stagingBuffer, cubemapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    // 5. Generate Mipmaps (GPU Blit) - This stays the same
    // We reuse the logic you already had which transitions to SHADER_READ_ONLY at the end
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = cubemapImage;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.layerCount = 6;

        int32_t mipWidth = cubemapSize;
        int32_t mipHeight = cubemapSize;

        for (uint32_t i = 1; i < numMipLevels; i++) {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            for (uint32_t face = 0; face < 6; face++) {
                VkImageBlit blit{};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.baseArrayLayer = face;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = face;
                blit.dstSubresource.layerCount = 1;

                vkCmdBlitImage(commandBuffer, cubemapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cubemapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            }

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        barrier.subresourceRange.baseMipLevel = numMipLevels - 1;
        barrier.subresourceRange.levelCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->initWithExistingImage(cubemapImage, cubemapMemory, format, cubemapSize, cubemapSize, 
                                 numMipLevels, 6, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return texture;
}


// Generate irradiance cubemap from environment map for diffuse IBL
// Generate irradiance cubemap from environment map for diffuse IBL


// In TextureUtils.cpp

std::shared_ptr<Texture> TextureUtils::createIrradianceMap(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    std::shared_ptr<Texture> environmentMap,
    const std::string& cacheKey, // <--- Added parameter
    const IBLConfig* customConfig)
{
    if (!environmentMap) return nullptr;
    
    const IBLConfig& config = customConfig ? *customConfig : iblConfig;
    const uint32_t irradianceSize = config.irradianceMapSize;
    
    // Try to load from cache
    std::vector<float> allPixelData;
    uint32_t cachedW, cachedH, cachedMips;
    bool loadedFromCache = false;

    if (!cacheKey.empty()) {
        std::string cachePath = getCachePath(cacheKey, "irradiance_" + std::to_string(irradianceSize));
        if (loadTextureCache(cachePath, allPixelData, cachedW, cachedH, cachedMips)) {
            if (cachedW == irradianceSize) {
                std::cout << "Loaded Irradiance Map from cache: " << cachePath << std::endl;
                loadedFromCache = true;
            }
        }
    }

    // Setup Vulkan Image
    VkImage irradianceImage;
    VkDeviceMemory irradianceMemory;
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = irradianceSize;
    imageInfo.extent.height = irradianceSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &irradianceImage) != VK_SUCCESS) return nullptr;
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, irradianceImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &irradianceMemory) != VK_SUCCESS) return nullptr;
    vkBindImageMemory(device, irradianceImage, irradianceMemory, 0);

    transitionImageLayout(device, commandPool, graphicsQueue, irradianceImage, format,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        0, 6, 0, 1);

    // --- CPU Generation Logic ---
    if (!loadedFromCache) {
        std::cout << "Generating Irradiance Map (High Quality)..." << std::endl;
        size_t faceSize = irradianceSize * irradianceSize * 4; // 4 floats per pixel
        allPixelData.resize(faceSize * 6);

        for (uint32_t face = 0; face < 6; face++) {
            #pragma omp parallel for
            for (uint32_t y = 0; y < irradianceSize; y++) {
                for (uint32_t x = 0; x < irradianceSize; x++) {
                    float u = (2.0f * (x + 0.5f) / irradianceSize) - 1.0f;
                    float v = (2.0f * (y + 0.5f) / irradianceSize) - 1.0f;
                    
                    glm::vec3 direction;
                    switch (face) {
                        case 0: direction = glm::normalize(glm::vec3(1.0f, -v, -u)); break;
                        case 1: direction = glm::normalize(glm::vec3(-1.0f, -v, u)); break;
                        case 2: direction = glm::normalize(glm::vec3(u, 1.0f, v)); break;
                        case 3: direction = glm::normalize(glm::vec3(u, -1.0f, -v)); break;
                        case 4: direction = glm::normalize(glm::vec3(u, -v, 1.0f)); break;
                        case 5: direction = glm::normalize(glm::vec3(-u, -v, -1.0f)); break;
                    }
                    
                    // Keep high quality sample count
                    glm::vec3 irradiance = diffuseConvolution(environmentMap, direction, 128); // 128 samples
                    
                    size_t pixelIdx = (face * irradianceSize * irradianceSize + y * irradianceSize + x) * 4;
                    allPixelData[pixelIdx + 0] = irradiance.r;
                    allPixelData[pixelIdx + 1] = irradiance.g;
                    allPixelData[pixelIdx + 2] = irradiance.b;
                    allPixelData[pixelIdx + 3] = 1.0f;
                }
            }
        }

        // Save to cache
        if (!cacheKey.empty()) {
            std::string cachePath = getCachePath(cacheKey, "irradiance_" + std::to_string(irradianceSize));
            saveTextureCache(cachePath, allPixelData, irradianceSize, irradianceSize, 1);
        }
    }

    // --- Upload to GPU ---
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VkDeviceSize bufferSize = allPixelData.size() * sizeof(float);
    
    createBuffer(device, physicalDevice, bufferSize, 
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);
    
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, allPixelData.data(), bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    size_t faceStride = irradianceSize * irradianceSize * 4 * sizeof(float);
    for (uint32_t face = 0; face < 6; face++) {
        VkBufferImageCopy region{};
        region.bufferOffset = face * faceStride;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = face;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {irradianceSize, irradianceSize, 1};
        
        // We need to manually submit copy commands here since copyBufferToImage helper assumes bufferOffset 0
        // Or modify copyBufferToImage. For brevity, let's assume a manual submit:
        
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &allocInfo, &cmd);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        vkCmdCopyBufferToImage(cmd, stagingBuffer, irradianceImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    }

    transitionImageLayout(device, commandPool, graphicsQueue, irradianceImage, format,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        0, 6, 0, 1);
    
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    texture->initWithExistingImage(irradianceImage, irradianceMemory, format, irradianceSize, irradianceSize, 
                                 1, 6, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return texture;
}






// Helper function to convert equirectangular projection to a cubemap face
// In TextureUtils.cpp - Replace the equirectangularToCubemapFace function

void TextureUtils::equirectangularToCubemapFace(
    float* equirectangularData, int equiWidth, int equiHeight, int channels,
    float* faceData, int faceSize, int faceIndex)
{
    // For each pixel in the cubemap face
    for (int y = 0; y < faceSize; y++) {
        for (int x = 0; x < faceSize; x++) {
            // Convert pixel coordinates to [-1, 1] range
            float u = 2.0f * (x + 0.5f) / faceSize - 1.0f;
            float v = 2.0f * (y + 0.5f) / faceSize - 1.0f;
            
            // Calculate the 3D direction vector for this pixel on the cube face
            glm::vec3 dir;
            
            switch (faceIndex) {
                case 0: // +X (Right)
                    dir = glm::vec3(1.0f, -v, -u);
                    break;
                case 1: // -X (Left)  
                    dir = glm::vec3(-1.0f, -v, u);
                    break;
                case 2: // +Y (Top)
                    dir = glm::vec3(u, 1.0f, v);
                    break;
                case 3: // -Y (Bottom)
                    dir = glm::vec3(u, -1.0f, -v);
                    break;
                case 4: // +Z (Front)
                    dir = glm::vec3(u, -v, 1.0f);
                    break;
                case 5: // -Z (Back)
                    dir = glm::vec3(-u, -v, -1.0f);
                    break;
            }
            
            // Normalize the direction vector
            dir = glm::normalize(dir);
            
            // Convert the 3D direction to spherical coordinates
            // Theta (azimuthal angle): angle from +X axis in XZ plane
            float theta = std::atan2(dir.z, dir.x);
            // Phi (polar angle): angle from +Y axis
            float phi = std::acos(glm::clamp(dir.y, -1.0f, 1.0f));
            
            // Convert spherical coordinates to equirectangular UV coordinates
            float eqU = (theta + glm::pi<float>()) / (2.0f * glm::pi<float>());
            float eqV = phi / glm::pi<float>();
            
            // Wrap UV coordinates
            eqU = glm::fract(eqU);
            eqV = glm::clamp(eqV, 0.0f, 1.0f);
            
            // Sample from the equirectangular image using bilinear interpolation
            float fX = eqU * (equiWidth - 1);
            float fY = eqV * (equiHeight - 1);
            
            int x0 = (int)std::floor(fX);
            int y0 = (int)std::floor(fY);
            int x1 = std::min(x0 + 1, equiWidth - 1);
            int y1 = std::min(y0 + 1, equiHeight - 1);
            
            float dx = fX - x0;
            float dy = fY - y0;
            
            // Ensure x coordinates wrap around
            x0 = x0 % equiWidth;
            x1 = x1 % equiWidth;
            
            // Bilinear interpolation
            int faceIdx = (y * faceSize + x) * 4;
            
            for (int c = 0; c < std::min(channels, 3); c++) {
                float v00 = equirectangularData[(y0 * equiWidth + x0) * channels + c];
                float v10 = equirectangularData[(y0 * equiWidth + x1) * channels + c];
                float v01 = equirectangularData[(y1 * equiWidth + x0) * channels + c];
                float v11 = equirectangularData[(y1 * equiWidth + x1) * channels + c];
                
                float v0 = v00 * (1.0f - dx) + v10 * dx;
                float v1 = v01 * (1.0f - dx) + v11 * dx;
                float value = v0 * (1.0f - dy) + v1 * dy;
                
                faceData[faceIdx + c] = value;
            }
            
            // Fill remaining channels if needed
            for (int c = channels; c < 3; c++) {
                faceData[faceIdx + c] = faceData[faceIdx]; // Use R channel
            }
            
            // Set alpha to 1.0
            faceData[faceIdx + 3] = 1.0f;
        }
    }
}

// Helper function for diffuse convolution
glm::vec3 TextureUtils::diffuseConvolution(std::shared_ptr<Texture> envMap, const glm::vec3& normal, int sampleCount) {
    // In a real implementation, we would sample a hemisphere around the normal
    // and perform proper convolution. This is a simplified approximation.
    
    // Generate a set of hemisphere samples
    std::vector<glm::vec3> samples = generateHemisphereSamples(normal, sampleCount);
    
    // Sample the environment map and accumulate the results
    glm::vec3 irradiance(0.0f);
    float weight = 0.0f;
    
    for (const auto& sample : samples) {
        // Calculate the angle between the normal and the sample direction
        float cosTheta = glm::dot(normal, sample);
        
        // Only consider samples in the hemisphere (cosTheta > 0)
        if (cosTheta > 0.0f) {
            // Sample the environment map
            glm::vec3 color = sampleEnvironmentMap(envMap, sample);
            
            // Weight by the cosine term and solid angle
            irradiance += color * cosTheta;
            weight += cosTheta;
        }
    }
    
    // Normalize the result
    if (weight > 0.0f) {
        irradiance /= weight;
    }
    
    return irradiance;
}

// Helper function for specular convolution
glm::vec3 TextureUtils::specularConvolution(std::shared_ptr<Texture> envMap, const glm::vec3& reflection, 
                                           float roughness, int sampleCount) {
    // In a real implementation, we would use importance sampling
    
    // Determine the lobe width based on roughness
    float alphaSquared = roughness * roughness;
    
    // Generate a set of samples around the reflection vector based on roughness
    std::vector<glm::vec3> samples = generateImportanceSamples(reflection, roughness, sampleCount);
    
    // Sample the environment map and accumulate the results
    glm::vec3 prefilteredColor(0.0f);
    float totalWeight = 0.0f;
    
    for (const auto& sample : samples) {
        // Calculate the angle between the reflection and the sample direction
        float NoS = glm::dot(reflection, sample);
        
        // Only consider samples that are visible from the reflection direction
        if (NoS > 0.0f) {
            // Sample the environment map
            glm::vec3 color = sampleEnvironmentMap(envMap, sample);
            
            // Apply importance sampling weight
            float D = distributionGGX(NoS, alphaSquared);
            float weight = D * NoS;
            
            prefilteredColor += color * weight;
            totalWeight += weight;
        }
    }
    
    // Normalize the result
    if (totalWeight > 0.0f) {
        prefilteredColor /= totalWeight;
    }
    
    return prefilteredColor;
}

bool Texture::initWithExistingImage(
    VkImage image, 
    VkDeviceMemory memory,
    VkFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    uint32_t layerCount,
    VkImageViewType viewType,
    VkImageLayout initialLayout)
{
    this->width=width;
    this->height=height;
    // Clean up existing resources
    if (textureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, textureSampler, nullptr);
        textureSampler = VK_NULL_HANDLE;
    }
    
    if (textureImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, textureImageView, nullptr);
        textureImageView = VK_NULL_HANDLE;
    }
    
    if (textureImage != VK_NULL_HANDLE && textureImage != image) {
        vkDestroyImage(device, textureImage, nullptr);
    }
    
    if (textureImageMemory != VK_NULL_HANDLE && textureImageMemory != memory) {
        vkFreeMemory(device, textureImageMemory, nullptr);
    }
    
    // Store new properties
    textureImage = image;
    textureImageMemory = memory;
    imageFormat = format;
    this->mipLevels = mipLevels;
    imageLayout = initialLayout;
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY
    };
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        std::cerr << "Failed to create texture image view!" << std::endl;
        return false;
    }
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    
    // Set address mode based on view type
    if (viewType == VK_IMAGE_VIEW_TYPE_CUBE) {
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    } else {
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
    
    // Check for anisotropic filtering support
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
    
    if (deviceFeatures.samplerAnisotropy) {
        samplerInfo.anisotropyEnable = VK_TRUE;
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        samplerInfo.maxAnisotropy = deviceProperties.limits.maxSamplerAnisotropy;
    } else {
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
    }
    
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        std::cerr << "Failed to create texture sampler!" << std::endl;
        return false;
    }
    
    return true;
}

// Add these implementations to TextureUtils.cpp

// Helper function implementations
uint32_t TextureUtils::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

void TextureUtils::createBuffer(
    VkDevice device, 
    VkPhysicalDevice physicalDevice,
    VkDeviceSize size, 
    VkBufferUsageFlags usage, 
    VkMemoryPropertyFlags properties, 
    VkBuffer& buffer, 
    VkDeviceMemory& bufferMemory) 
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void TextureUtils::transitionImageLayout(
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t baseArrayLayer,
    uint32_t layerCount,
    uint32_t baseMipLevel,
    uint32_t levelCount)
{
    // Create command buffer for the transition
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    // Begin command buffer recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Create image barrier
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = layerCount;

    // Determine pipeline stages and access masks based on layouts
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        throw std::runtime_error("Unsupported layout transition!");
    }

    // Record the barrier command
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // End and submit command buffer
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void TextureUtils::copyBufferToImage(
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height,
    uint32_t baseArrayLayer,
    uint32_t mipLevel)
{
    // Create command buffer for the copy
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    // Begin command buffer recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Define the region to copy
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = baseArrayLayer;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    // Record the copy command
    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // End and submit command buffer
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

std::shared_ptr<Texture> TextureUtils::createDefaultEnvironmentCubemap(
    VkDevice device, 
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool, 
    VkQueue graphicsQueue)
{
    // Create a simple gradient environment cubemap
    const uint32_t size = 256; // Size of each cubemap face
    std::vector<unsigned char> pixels(size * size * 4 * 6); // 6 faces
    
    // Generate gradient data for each face
    for (uint32_t face = 0; face < 6; face++) {
        for (uint32_t y = 0; y < size; y++) {
            for (uint32_t x = 0; x < size; x++) {
                // Calculate normalized coordinates
                float u = (x / (float)(size - 1)) * 2.0f - 1.0f;
                float v = (y / (float)(size - 1)) * 2.0f - 1.0f;
                
                // Choose face-specific gradient
                glm::vec3 color;
                switch (face) {
                    case 0: // +X (right), red gradient
                        color = glm::vec3(0.8f, 0.2f + 0.2f * v, 0.2f + 0.2f * u);
                        break;
                    case 1: // -X (left), cyan gradient
                        color = glm::vec3(0.2f, 0.8f - 0.2f * v, 0.8f - 0.2f * u);
                        break;
                    case 2: // +Y (up), blue gradient
                        color = glm::vec3(0.2f + 0.2f * u, 0.2f + 0.2f * v, 0.8f);
                        break;
                    case 3: // -Y (down), yellow gradient
                        color = glm::vec3(0.8f - 0.2f * u, 0.8f - 0.2f * v, 0.2f);
                        break;
                    case 4: // +Z (front), green gradient
                        color = glm::vec3(0.2f + 0.2f * u, 0.8f, 0.2f + 0.2f * v);
                        break;
                    case 5: // -Z (back), magenta gradient
                        color = glm::vec3(0.8f - 0.2f * u, 0.2f, 0.8f - 0.2f * v);
                        break;
                }
                
                // Set pixel data
                uint32_t index = (face * size * size + y * size + x) * 4;
                pixels[index + 0] = static_cast<unsigned char>(color.r * 255.0f);
                pixels[index + 1] = static_cast<unsigned char>(color.g * 255.0f);
                pixels[index + 2] = static_cast<unsigned char>(color.b * 255.0f);
                pixels[index + 3] = 255; // Full alpha
            }
        }
    }
    
    // Create a texture from the pixel data
    auto texture = std::make_shared<Texture>(device, physicalDevice);
    // Note: This is a simplification - for a proper cubemap, you would use a more specialized texture creation method
    texture->createFromPixels(pixels.data(), size, size * 6, 4, commandPool, graphicsQueue);
    
    std::cout << "Created default environment cubemap" << std::endl;
    return texture;
}

std::vector<glm::vec3> TextureUtils::generateHemisphereSamples(const glm::vec3& normal, int sampleCount) {
    std::vector<glm::vec3> samples;
    samples.reserve(sampleCount);
    
    // Create a simple basis with the normal
    glm::vec3 tangent;
    if (std::abs(normal.x) > std::abs(normal.z)) {
        tangent = glm::vec3(-normal.y, normal.x, 0.0f);
    } else {
        tangent = glm::vec3(0.0f, -normal.z, normal.y);
    }
    tangent = glm::normalize(tangent);
    glm::vec3 bitangent = glm::cross(normal, tangent);
    
    // Generate samples in hemisphere
    for (int i = 0; i < sampleCount; i++) {
        // Use Hammersley sequence for uniform sampling
        float u = static_cast<float>(i) / static_cast<float>(sampleCount);
        float v = float(((i * 16807) % 2147483647) / 2147483647.0f);
        
        // Convert to spherical coordinates (hemisphere)
        float phi = 2.0f * glm::pi<float>() * u;
        float theta = std::acos(v);
        
        // Convert to cartesian coordinates
        float x = std::sin(theta) * std::cos(phi);
        float y = std::sin(theta) * std::sin(phi);
        float z = std::cos(theta);
        
        // Transform to world space using the normal's basis
        glm::vec3 sample = tangent * x + bitangent * y + normal * z;
        samples.push_back(glm::normalize(sample));
    }
    
    return samples;
}

std::vector<glm::vec3> TextureUtils::generateImportanceSamples(
    const glm::vec3& reflection, 
    float roughness, 
    int sampleCount) 
{
    std::vector<glm::vec3> samples;
    samples.reserve(sampleCount);
    
    // Create basis vectors
    glm::vec3 N = reflection;
    glm::vec3 tangent;
    if (std::abs(N.x) > std::abs(N.z)) {
        tangent = glm::vec3(-N.y, N.x, 0.0f);
    } else {
        tangent = glm::vec3(0.0f, -N.z, N.y);
    }
    tangent = glm::normalize(tangent);
    glm::vec3 bitangent = glm::cross(N, tangent);
    
    // The alpha parameter for GGX distribution
    float alpha = roughness * roughness;
    
    // Generate samples
    for (int i = 0; i < sampleCount; i++) {
        // Use Hammersley sequence for uniform sampling
        float u = static_cast<float>(i) / static_cast<float>(sampleCount);
        float v = float(((i * 16807) % 2147483647) / 2147483647.0f);
        
        // GGX importance sampling (corrected formula)
        float phi = 2.0f * glm::pi<float>() * u;
        float cosTheta = std::sqrt((1.0f - v) / (1.0f + (alpha*alpha - 1.0f) * v));
        float theta = std::acos(cosTheta);
        
        // Convert to cartesian coordinates (in tangent space)
        float x = std::sin(theta) * std::cos(phi);
        float y = std::sin(theta) * std::sin(phi);
        float z = std::cos(theta);
        
        // Transform to world space
        glm::vec3 sample = tangent * x + bitangent * y + N * z;
        samples.push_back(glm::normalize(sample));
    }
    
    return samples;
}

glm::vec3 TextureUtils::sampleEnvironmentMap(std::shared_ptr<Texture> envMap, const glm::vec3& direction) {
    // This is a placeholder function that would sample the environment map at a given direction
    // In a real implementation, you would need to convert the direction to texture coordinates
    // and sample the environment map
    
    // For this simplified version, we'll just return a sky-like color based on the direction
    float y = direction.y * 0.5f + 0.5f; // Map from [-1,1] to [0,1]
    
    // Sky gradient from blue to white
    glm::vec3 skyColor = glm::mix(
        glm::vec3(1.0f, 1.0f, 1.0f),  // Horizon color
        glm::vec3(0.3f, 0.5f, 0.9f),  // Sky color
        y
    );
    
    // Add a simple sun
    glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.5f, 0.5f));
    float sunDot = glm::max(0.0f, glm::dot(direction, sunDir));
    float sunIntensity = std::pow(sunDot, 64.0f) * 2.0f;
    
    // Add sun color
    glm::vec3 finalColor = skyColor + glm::vec3(1.0f, 0.9f, 0.7f) * sunIntensity;
    
    return finalColor;
}

float TextureUtils::distributionGGX(float NoH, float alphaSquared) {
    // GGX/Trowbridge-Reitz normal distribution function
    float denom = NoH * NoH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / (glm::pi<float>() * denom * denom);
}

glm::vec2 TextureUtils::integrateBRDF(float NoV, float roughness) {
    // Simplified BRDF integration approximation
    const float kEpsilon = 1e-5f;
    
    // Avoid singularity
    NoV = glm::max(NoV, kEpsilon);
    
    // Simple approximation for scale and bias
    float scale = 1.0f - std::pow(1.0f - NoV, 5.0f * (1.0f - roughness));
    float bias = roughness * 0.25f * (1.0f - NoV);
    
    return glm::vec2(scale, bias);
}