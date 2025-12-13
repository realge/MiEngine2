#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>
#include <array> 
#include <vector>
#include <optional>
#include "include/mesh/Mesh.h"
#include <fstream>
#include <chrono>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "include/Utils/CommonVertex.h"
#include "include/texture/Texture.h"
#include <set>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "include/Utils/TextureUtils.h"
#include "include/scene/Scene.h"
#include "include/camera/Camera.h"

#include <memory>

#include "include/debug/DebugUIManager.h"
#include "include/Renderer/IBLSystem.h"
#include "include/Renderer/ShadowSystem.h"
#include "include/Renderer/PointLightShadowSystem.h"
#include "include/Renderer/WaterSystem.h"
#include "include/Utils/SkeletalVertex.h"
#include "include/asset/AssetBrowserWindow.h"
#include "include/asset/MeshLibrary.h"
#include "include/core/MiWorld.h"
#include "include/raytracing/RayTracingSystem.h"
#include "include/virtualgeo/VirtualGeoRenderer.h"

class IBLSystem;
class ShadowSystem;
class PointLightShadowSystem;
class WaterSystem;

enum class RenderMode {
    Standard,
    PBR,
    PBR_IBL
};

// Render statistics for performance monitoring
struct RenderStats {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t vertices = 0;

    void reset() {
        drawCalls = 0;
        triangles = 0;
        vertices = 0;
    }
};



struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

struct PushConstant {//push constant for pbr
    alignas(16) glm::mat4 model;           // Model matrix
    alignas(16) glm::vec4 baseColorFactor; // RGB + alpha
    alignas(4) float metallicFactor;
    alignas(4) float roughnessFactor;
    alignas(4) float ambientOcclusion;
    alignas(4) float emissiveFactor;
    alignas(4) int hasAlbedoMap;
    alignas(4) int hasNormalMap;
    alignas(4) int hasMetallicRoughnessMap;
    alignas(4) int hasEmissiveMap;
    alignas(4) int hasOcclusionMap;
    alignas(4) int debugLayer;  // 0=full, 1=direct, 2=diffuse IBL, 3=specular IBL, 4-5=BRDF LUT, 6=prefilter, 7=ambient
    alignas(4) int useIBL;      // 0=off, 1=on
    alignas(4) float iblIntensity; // Multiplier for IBL intensity
    alignas(4) int useRT;          // 0=off, 1=on (ray traced reflections/shadows)
    alignas(4) float rtBlendFactor; // Blend factor for RT reflections (0-1)
    alignas(4) int useRTReflections; // 0=off, 1=on (RT reflections specifically)
    alignas(4) int useRTShadows;     // 0=off, 1=on (RT shadows specifically)
};
struct SkyboxPushConstant {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};


struct MaterialUniformBuffer {
    alignas(16) glm::vec4 baseColorFactor;    // RGB + alpha
    alignas(4) float metallicFactor;
    alignas(4) float roughnessFactor;
    alignas(4) float aoStrength;
    alignas(4) float emissiveStrength;
    
    // Flags for which textures are available (0 = not used, 1 = used)
    alignas(4) int hasBaseColorMap;
    alignas(4) int hasNormalMap;
    alignas(4) int hasMetallicRoughnessMap;
    alignas(4) int hasOcclusionMap;
    alignas(4) int hasEmissiveMap;
    
    // Additional parameters
    alignas(4) float alphaCutoff;
    alignas(4) int alphaMode;  // 0 = opaque, 1 = mask, 2 = blend
    alignas(8) glm::vec2 padding;
};

struct ShadowUniformBuffer {
    glm::mat4 lightSpaceMatrix; };

class VulkanRenderer
{

    //=============shadow system================
    std::unique_ptr<ShadowSystem> shadowSystem;
    std::unique_ptr<PointLightShadowSystem> pointLightShadowSystem;

    //=============water system================
    std::unique_ptr<WaterSystem> waterSystem;

    // Point light shadow info buffer (for shader)
    struct PointLightShadowInfoBuffer {
        glm::vec4 positionAndFarPlane[8];  // xyz = position, w = far plane
        int shadowLightCount;
        int padding[3];
    };
    std::vector<VkBuffer> pointLightShadowInfoBuffers;
    std::vector<VkDeviceMemory> pointLightShadowInfoBuffersMemory;
    std::vector<void*> pointLightShadowInfoBuffersMapped;
    
// PERFORMANCE NOTE: Validation layers add 10-100x overhead per Vulkan call!
// Always test performance in Release builds
#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

private:
    // Get texture image info with fallback to default textures
    VkDescriptorImageInfo getTextureImageInfo(const std::shared_ptr<Texture>& texture, 
                                             std::shared_ptr<Texture> defaultTexture);
    void createPBRTestScene();

//=============camera system================
private:
    // Camera system
    std::unique_ptr<Camera> camera;

    
    // Input state handled by Input system
    
    // Timing
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    bool m_AutoUpdateCamera = true;


   
public:
    // Input callbacks handled by Input system

    void updateCamera(float deltaTime, bool enableInput = true, bool enableMovement = true);
    void setAutoUpdateCamera(bool enable) { m_AutoUpdateCamera = enable; }
    Camera* getCamera() const { return camera.get(); }
 
    Scene* getScene() const { return scene.get(); }
  
    VkExtent2D getSwapChainExtent() const { return swapChainExtent; }
    float getNearPlane() const { return nearPlane; }
    float getFarPlane() const { return farPlane; }
    bool isPBRPipelineReady() const { return pbrPipeline != VK_NULL_HANDLE; }
    bool isSkeletalPipelineReady() const { return skeletalPipeline != VK_NULL_HANDLE; }

    // Skeletal rendering support
    VkPipeline getSkeletalPipeline() const { return skeletalPipeline; }
    VkPipelineLayout getSkeletalPipelineLayout() const { return skeletalPipelineLayout; }
    void createSkeletalInstanceResources(uint32_t instanceId, uint32_t boneCount);
    void updateBoneMatrices(uint32_t instanceId, const std::vector<glm::mat4>& boneMatrices, uint32_t frameIndex);
    VkDescriptorSet getBoneMatrixDescriptorSet(uint32_t instanceId, uint32_t frameIndex);
    VkDescriptorSetLayout getBoneMatrixDescriptorSetLayout() const { return boneMatrixDescriptorSetLayout; }
    void cleanupSkeletalInstanceResources(uint32_t instanceId);

    // Render statistics
    const RenderStats& getRenderStats() const { return renderStats; }
    void addDrawCall(uint32_t vertexCount, uint32_t indexCount);
    void resetRenderStats() { renderStats.reset(); }
   
    bool isSkyboxReady() const { return skyboxPipeline != VK_NULL_HANDLE; }
    
    // Helper methods for single time commands (if not already present)
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    bool mouseCaptured = false;
    // Getters for GLFW callbacks
    GLFWwindow* getWindow() { return window; }

    
//=================================

//=============debug system================
public:
    std::unique_ptr<DebugUIManager> debugUI;
    std::unique_ptr<MiEngine::AssetBrowserWindow> assetBrowser;
    std::unique_ptr<MiEngine::MiWorld> world;
    std::unique_ptr<MiEngine::MeshLibrary> meshLibrary;

    // Getter for Asset Browser
    MiEngine::AssetBrowserWindow* getAssetBrowser() { return assetBrowser.get(); }

    // Getter for World (Actor System)
    MiEngine::MiWorld* getWorld() { return world.get(); }

    // Getter for MeshLibrary
    MiEngine::MeshLibrary& getMeshLibrary() { return *meshLibrary; }

public:
   
    // IBL setup methods
    
    // Getter for IBL system
    IBLSystem* getIBLSystem() { return iblSystem.get(); }

    // Getter for default texture (for fallback in RT system)
    std::shared_ptr<Texture> getDefaultTexture() { return defaultTexture; }
    
    // Getter for Shadow system
    ShadowSystem* getShadowSystem() { return shadowSystem.get(); }
    PointLightShadowSystem* getPointLightShadowSystem() { return pointLightShadowSystem.get(); }

    // Getter for Water system
    WaterSystem* getWaterSystem() { return waterSystem.get(); }
    void initializeWater(uint32_t resolution = 256);

    // Get render pass for subsystems
    VkRenderPass getRenderPass() const { return renderPass; }

    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }
    uint32_t getMaxFramesInFlight() const { return MAX_FRAMES_IN_FLIGHT; }
    
    PushConstant createPushConstant(const glm::mat4& model, const Material& material);
    
    void updateViewProjection(const glm::mat4& view, const glm::mat4& proj);
    // Create a descriptor set for a specific material
    VkDescriptorSet createMaterialDescriptorSet(const Material& material);
    
    // Helper methods for shadow system and other subsystems
    VkFormat findDepthFormat();
    void createImage(uint32_t width, uint32_t height, VkFormat format,
                    VkImageTiling tiling, VkImageUsageFlags usage,
                    VkMemoryPropertyFlags properties, VkImage& image,
                    VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    
    // Update texture information in an existing descriptor set
    void updateDescriptorSetTextures(VkDescriptorSet descriptorSet, const Material& material);
    
    // Bind a descriptor set (without updating it)
    void bindDescriptorSet(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet);
    VkPipelineLayout getPipelineLayout();

    //==================================================================== debugging
    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    // Debug messenger handle
    VkDebugUtilsMessengerEXT debugMessenger;

    // Helper function prototypes
    bool checkValidationLayerSupport();
    void setupDebugMessenger();
    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, 
                                          const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, 
                                          const VkAllocationCallbacks* pAllocator, 
                                          VkDebugUtilsMessengerEXT* pDebugMessenger);
    void DestroyDebugUtilsMessengerEXT(VkInstance instance, 
                                      VkDebugUtilsMessengerEXT debugMessenger, 
                                      const VkAllocationCallbacks* pAllocator);
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    //====================================================================
    struct UniformBufferObject {
        glm::mat4 model;                    // offset 0   (64 bytes)
        glm::mat4 view;                     // offset 64  (64 bytes)
        glm::mat4 proj;                     // offset 128 (64 bytes)
        glm::vec4 cameraPos;                // offset 192 (16 bytes) - use vec4 to match std140 vec3 padding
        float time;                         // offset 208 (4 bytes)
        float maxReflectionLod;             // offset 212 (4 bytes)
        glm::vec2 _padding;                 // offset 216 (8 bytes) - pad to reach offset 224
        glm::mat4 lightSpaceMatrix;         // offset 224 (64 bytes)
    };
    
public: //light related
    struct LightData {
        alignas(16) glm::vec4 position;   // xyz = position/direction, w = 1 for point, 0 for directional
        alignas(16) glm::vec4 color;      // rgb = color, a = intensity
        alignas(4) float radius;
        alignas(4) float falloff;
        alignas(8) float padding[2];      // Padding to ensure 16-byte alignment
    };

    #define MAX_LIGHTS 16

    struct LightUniformBuffer {
        alignas(16) LightData lights[MAX_LIGHTS];  // Put lights first to match shader
        alignas(16) glm::vec4 ambientColor;
        alignas(4) int lightCount;
        alignas(4) int padding[3];        // Padding to ensure 16-byte alignment
    };

    // Add light buffer members
    std::vector<VkBuffer> lightUniformBuffers;
    std::vector<VkDeviceMemory> lightUniformBuffersMemory;
    std::vector<void*> lightUniformBuffersMapped;
    VkDescriptorSetLayout lightDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> lightDescriptorSets;

    void createLightUniformBuffers();
    void createLightDescriptorSets();
    void updateLights();
   
public:

    
    void createDefaultTextures();
    void createMaterialUniformBuffers();
    
    void updateAllTextureDescriptors(const Material& material);
    ModelLoader modelLoader;
    VkPipeline getGraphicsPipeline () const { return graphicsPipeline; } // getGraphicsPipeline()
    VkPipeline getPBRPipeline() const { return pbrPipeline; }
    VkPipelineLayout getPBRPipelineLayout() const { return pbrPipelineLayout; }
    const std::vector<VkDescriptorSet>& getMVPDescriptorSets() const { return mvpDescriptorSets; }
    const std::vector<VkDescriptorSet>& getLightDescriptorSets() const { return lightDescriptorSets; }
    RenderMode getRenderMode() const { return renderMode; }

private:
    std::vector<VkFence> imagesInFlight;

    //skybox related resources
    VkPipelineLayout skyboxPipelineLayout;
    VkPipeline skyboxPipeline;
    VkDescriptorSetLayout skyboxDescriptorSetLayout;
    std::vector<VkDescriptorSet> skyboxDescriptorSets;
    std::shared_ptr<Mesh> skyboxMesh;
    VkDescriptorSet placeholderIBLSet = VK_NULL_HANDLE;//TODO: implement 2 pipeline pbr_ibl and prb_noIbl later
    // IBL-related resources
    std::unique_ptr<IBLSystem> iblSystem;
    VkDescriptorSetLayout iblDescriptorSetLayout;
    VkDescriptorSet iblDescriptorSet;
    VkPipelineLayout pbrPipelineLayout;  // Pipeline layout for PBR rendering
    VkPipeline pbrPipeline;              // PBR pipeline

    // Skeletal animation pipeline resources
    VkPipelineLayout skeletalPipelineLayout = VK_NULL_HANDLE;
    VkPipeline skeletalPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout boneMatrixDescriptorSetLayout = VK_NULL_HANDLE;

    // Dummy RT output descriptor set layout and set (for pipelines when RT not ready)
    VkDescriptorSetLayout dummyRTOutputDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> dummyRTOutputDescriptorSets;  // One per frame in flight

    // Per-instance bone matrix UBOs (indexed by instance ID)
    struct SkeletalInstanceData {
        std::vector<VkBuffer> boneMatrixBuffers;          // One per frame in flight
        std::vector<VkDeviceMemory> boneMatrixMemory;
        std::vector<void*> boneMatrixMapped;
        std::vector<VkDescriptorSet> boneMatrixDescriptorSets;
    };
    std::unordered_map<uint32_t, SkeletalInstanceData> skeletalInstances;

    // Current rendering mode
    RenderMode renderMode = RenderMode::Standard;
    int debugLayerMode = 0;  // 0=full rendering, 1-7=debug layers
    float iblIntensity = 1.0f;

    // Render statistics
    RenderStats renderStats;
    std::shared_ptr<Texture> defaultAlbedoTexture;
    std::shared_ptr<Texture> defaultNormalTexture;
    std::shared_ptr<Texture> defaultMetallicRoughnessTexture;
    std::shared_ptr<Texture> defaultOcclusionTexture;
    std::shared_ptr<Texture> defaultEmissiveTexture;
    
    // Material UBO buffers
    std::vector<VkBuffer> materialUniformBuffers;
    std::vector<VkDeviceMemory> materialUniformBuffersMemory;
    std::vector<void*> materialUniformBuffersMapped;
    
    std::shared_ptr<Texture> environmentMap; // Cubemap for reflections
    std::shared_ptr<Texture> irradianceMap;  // Diffuse environment lighting
    std::shared_ptr<Texture> prefilterMap;   // Prefiltered specular environment
    std::shared_ptr<Texture> brdfLUT;

    // ========================================================================
    // Ray Tracing System
    // ========================================================================
    bool m_RayTracingSupported = false;
    std::unique_ptr<MiEngine::RayTracingSystem> rayTracingSystem;

public:
    // Ray tracing accessors
    bool isRayTracingSupported() const { return m_RayTracingSupported; }
    MiEngine::RayTracingSystem* getRayTracingSystem() const { return rayTracingSystem.get(); }

    // Initialize ray tracing (call after device creation)
    bool initRayTracing();

    // ========================================================================
    // Virtual Geometry System
    // ========================================================================
private:
    std::unique_ptr<MiEngine::VirtualGeoRenderer> m_virtualGeoRenderer;

public:
    // Virtual Geometry accessors
    MiEngine::VirtualGeoRenderer* getVirtualGeoRenderer() const { return m_virtualGeoRenderer.get(); }

    // Initialize virtual geometry renderer
    bool initVirtualGeo();

private:
    std::shared_ptr<Texture> defaultTexture;
    // Create a default white texture
    void createDefaultTexture();
    void createSkyboxPipeline();
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;

    // Helper methods for depth resources (private)
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates,
                                VkImageTiling tiling,
                                VkFormatFeatureFlags features);
    bool hasStencilComponent(VkFormat format);
    void initializeDebugUI();
    void createDepthResources();
public:
    VkDevice getDevice() const { return device; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkCommandPool getCommandPool() const { return commandPool; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkImage getDepthImage() const { return depthImage; }
    void recreateSwapChain();
    void cleanupSwapChain();

public:
    std::vector<VkDescriptorSet> mvpDescriptorSets;
    
private:
   //TODO::need to refactor the code later
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;
    // Uniform buffer members
    float rotationAngle = 0.0f;

    // Descriptor members
    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout mvpDescriptorSetLayout;

    VkDescriptorSetLayout materialDescriptorSetLayout;
     std::vector<VkDescriptorSet>  materialDescriptorSets;
 
    std::vector<MeshData> meshes;

    std::unique_ptr<Scene> scene;
    
    // Camera properties
    glm::vec3 cameraPos = glm::vec3(2.0f, 2.0f, 2.0f);
    glm::vec3 cameraTarget = glm::vec3(0.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 10.0f;

    //====================================================================
    
   
    
        GLFWwindow* window;
        VkInstance instance;
        VkSurfaceKHR surface;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device;
        VkQueue graphicsQueue, presentQueue;
        VkSwapchainKHR swapChain;
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkImageView> swapChainImageViews;
        VkRenderPass renderPass;
        VkPipelineLayout pipelineLayout;
        VkPipeline graphicsPipeline;
        std::vector<VkFramebuffer> swapChainFramebuffers;
        VkCommandPool commandPool;
        std::vector<VkCommandBuffer> commandBuffers;

        // Synchronization objects
        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        size_t currentFrame = 0;
        const int MAX_FRAMES_IN_FLIGHT = 2;
    
  
    
   
    public:
        VulkanRenderer();
       
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        
        void initVulkan();
    bool setupIBL(const std::string& hdriPath);
    bool isIBLReady() const { return iblSystem && iblSystem->isReady(); }
    void setRenderMode(RenderMode mode) { renderMode = mode; }
    void setDebugLayer(int layer) { debugLayerMode = layer; }
    int getDebugLayer() const { return debugLayerMode; }
    void setIBLIntensity(float intensity) { iblIntensity = intensity; }
    float getIBLIntensity() const { return iblIntensity; }
    void createInstance();
        void createSurface();
        void pickPhysicalDevice();

        void createLogicalDevice();

        void createSwapChain();

        void createImageViews();
        void createRenderPass();
    void createPBRPipeline();
    void createSkeletalPipeline();
    void createBoneMatrixDescriptorSetLayout();
    void createGraphicsPipeline();
    void createLightDescriptorSetLayout();
    void createFramebuffers();
        void createCommandPool();
        
        void createCommandBuffers();
        void createSyncObjects();
        void run();
        void initWindow();
        void mainLoop();
    void createPBRIBLTestScene(); // Queues the load
    
private:
    bool pendingTestSceneLoad = false;
    void loadSphereGridScene(); // Actual implementation

    // Deferred IBL update members
    std::string pendingIBLPath;
    bool isIBLUpdatePending = false;
    void processPendingIBLUpdate();

public:
    void drawFrame();
    void createDescriptorSetLayouts();
   
       
        void createUniformBuffers();
        void createDescriptorPool();
        void createDescriptorSets();
    void updateMVPMatrices(const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj);
        void cleanup();

        bool isDeviceSuitable(VkPhysicalDevice device);

        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
public:

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
                }
        }

        throw std::runtime_error("failed to find suitable memory type!");
    }

    
    std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error("failed to open file: " + filename);
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("failed to create shader module!");
        return module;
    }
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties, VkBuffer& buffer,
        VkDeviceMemory& bufferMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        vkBindBufferMemory(device, buffer, bufferMemory, 0);
    }

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
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

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

};

