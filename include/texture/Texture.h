#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <memory>

class Texture {
public:
    Texture(VkDevice device, VkPhysicalDevice physicalDevice);
    ~Texture();

    // Getters
    VkImage getImage() const { return textureImage; }
    VkFormat getFormat() const { return imageFormat; }
    uint32_t getMipLevels() const { return mipLevels; }
    
    // Load texture from a file (use stb_image internally)
    bool loadFromFile(const std::string& filepath, VkCommandPool commandPool, VkQueue graphicsQueue);
    
    // For creating a texture from raw pixel data
    bool createFromPixels(const unsigned char* pixels, uint32_t width, uint32_t height, 
                         uint32_t channels, VkCommandPool commandPool, VkQueue graphicsQueue);
    
    // Get the texture image view for binding
    VkImageView getImageView() const { return textureImageView; }
    
    // Get the texture sampler
    VkSampler getSampler() const { return textureSampler; }
    
    // Get image layout
    VkImageLayout getImageLayout() const { return imageLayout; }
    bool initWithExistingImage(VkImage image, VkDeviceMemory memory, VkFormat format, uint32_t width, uint32_t height,
                               uint32_t mipLevels, uint32_t layerCount, VkImageViewType viewType,
                               VkImageLayout initialLayout);

    uint32_t getWidth() const { return width; }       // ADD THIS
    uint32_t getHeight() const { return height; } 

private:
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    
    VkImage textureImage = VK_NULL_HANDLE;
    VkDeviceMemory textureImageMemory = VK_NULL_HANDLE;
    VkImageView textureImageView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    uint32_t width = 0;         // ADD THIS
    uint32_t height = 0;        // ADD THIS
    uint32_t mipLevels = 1;
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    // Helper methods
    void createTextureImage(const unsigned char* pixels, uint32_t width, uint32_t height, uint32_t channels,
                          VkCommandPool commandPool, VkQueue graphicsQueue);
    void createTextureImageView();
    void createTextureSampler();
    void transitionImageLayout(VkImage image, VkFormat format,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkCommandPool commandPool, VkQueue graphicsQueue);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                          VkCommandPool commandPool, VkQueue graphicsQueue);
    void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels,
                       VkCommandPool commandPool, VkQueue graphicsQueue);
                       
    VkCommandBuffer beginSingleTimeCommands(VkCommandPool commandPool);
    void endSingleTimeCommands(VkCommandBuffer commandBuffer, VkCommandPool commandPool, VkQueue graphicsQueue);
};