#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

// Forward declarations
class VulkanRenderer;
class Texture;

/**
 * @brief Parameters for water simulation and rendering
 */
struct WaterParameters {
    // Simulation parameters
    float waveSpeed = 2.0f;           // Speed of wave propagation
    float damping = 0.98f;            // Wave energy decay per frame
    float heightScale = 0.5f;         // Maximum visual wave height

    // Visual parameters
    float refractionStrength = 0.1f;  // Underwater distortion amount
    float fresnelPower = 5.0f;        // Fresnel effect intensity
    float specularPower = 256.0f;     // Water specular highlight sharpness
    float reflectionStrength = 1.0f;  // IBL reflection intensity

    // Foam parameters (for future use)
    float foamThreshold = 0.3f;       // Height threshold for foam
    float foamIntensity = 1.0f;       // Foam brightness
    float edgeFoamWidth = 0.5f;       // Width of shore foam

    // Water appearance
    glm::vec3 shallowColor = glm::vec3(0.0f, 0.5f, 0.5f);
    glm::vec3 deepColor = glm::vec3(0.0f, 0.1f, 0.2f);
    float depthFalloff = 2.0f;        // Depth-based color transition rate
};

/**
 * @brief Ripple point for interactive water disturbance
 */
struct RipplePoint {
    glm::vec2 position;               // UV coordinates (0-1)
    float strength;                   // Ripple intensity
    float radius;                     // Ripple initial radius
};

/**
 * @brief GPU-based water simulation and rendering system
 *
 * This class handles height-field water simulation using compute shaders
 * and renders the water surface with reflections and basic lighting.
 * This is the first compute shader system in the engine.
 */
class WaterSystem {
public:
    /**
     * @brief Construct a new Water System
     * @param renderer Pointer to the VulkanRenderer instance
     */
    explicit WaterSystem(VulkanRenderer* renderer);

    /**
     * @brief Destroy the Water System and free resources
     */
    ~WaterSystem();

    /**
     * @brief Initialize the water system
     * @param gridResolution Resolution of the height field (default 256x256)
     * @return true if initialization succeeded
     */
    bool initialize(uint32_t gridResolution = 256);

    /**
     * @brief Cleanup all created resources
     */
    void cleanup();

    /**
     * @brief Update water simulation (dispatch compute shaders)
     * @param commandBuffer Command buffer to record compute commands
     * @param deltaTime Time since last frame in seconds
     * @param frameIndex Current frame index for descriptor sets
     */
    void update(VkCommandBuffer commandBuffer, float deltaTime, uint32_t frameIndex);

    /**
     * @brief Add a ripple disturbance to the water
     * @param position UV position (0-1 range)
     * @param strength Ripple intensity (default 1.0)
     * @param radius Ripple radius in UV space (default 0.05)
     */
    void addRipple(const glm::vec2& position, float strength = 1.0f, float radius = 0.05f);

    /**
     * @brief Render the water surface
     * @param commandBuffer Command buffer to record draw commands
     * @param view View matrix
     * @param projection Projection matrix
     * @param cameraPos Camera position in world space
     * @param frameIndex Current frame index for descriptor sets
     */
    void render(VkCommandBuffer commandBuffer,
                const glm::mat4& view,
                const glm::mat4& projection,
                const glm::vec3& cameraPos,
                uint32_t frameIndex);

    // Configuration
    void setParameters(const WaterParameters& params) { parameters = params; }
    WaterParameters& getParameters() { return parameters; }
    const WaterParameters& getParameters() const { return parameters; }

    void setPosition(const glm::vec3& pos) { position = pos; }
    glm::vec3 getPosition() const { return position; }

    void setScale(const glm::vec3& s) { scale = s; }
    glm::vec3 getScale() const { return scale; }

    /**
     * @brief Check if the water system is ready for rendering
     */
    bool isReady() const { return initialized; }

    /**
     * @brief Recreate graphics pipeline (call when IBL becomes available)
     */
    void recreateGraphicsPipeline();

    /**
     * @brief Get the height map image view (for debugging/external use)
     */
    VkImageView getHeightMapView() const { return heightMapViews[currentHeightMap]; }

    /**
     * @brief Get the normal map image view
     */
    VkImageView getNormalMapView() const { return normalMapView; }

private:
    // Renderer reference
    VulkanRenderer* renderer;

    // Water parameters
    WaterParameters parameters;

    // Transform
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(10.0f, 1.0f, 10.0f);

    // Grid configuration
    uint32_t gridResolution = 256;
    uint32_t meshResolution = 64;  // Vertices per side for rendering mesh
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    // State
    bool initialized = false;
    int currentHeightMap = 1;  // Index for current state (t) - initialized in createHeightMaps
    float accumulatedTime = 0.0f;

    // Height field textures (3 buffers for wave simulation: previous, current, output)
    VkImage heightMaps[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory heightMapMemory[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkImageView heightMapViews[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    int previousHeightMap = 0;  // Index for previous state (t-1)
    int outputHeightMap = 2;    // Index for output state (t+1)

    // Normal map (generated from height field)
    VkImage normalMap = VK_NULL_HANDLE;
    VkDeviceMemory normalMapMemory = VK_NULL_HANDLE;
    VkImageView normalMapView = VK_NULL_HANDLE;

    // Samplers
    VkSampler heightMapSampler = VK_NULL_HANDLE;
    VkSampler normalMapSampler = VK_NULL_HANDLE;

    // Water mesh (grid)
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;

    // Compute pipeline - Wave simulation
    VkPipeline waveComputePipeline = VK_NULL_HANDLE;
    VkPipelineLayout waveComputePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout waveComputeDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> waveComputeDescriptorSets;  // One per frame in flight

    // Compute pipeline - Normal generation
    VkPipeline normalComputePipeline = VK_NULL_HANDLE;
    VkPipelineLayout normalComputePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout normalComputeDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> normalComputeDescriptorSets; // One per frame in flight

    // Graphics pipeline - Water rendering
    VkPipeline waterGraphicsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout waterGraphicsPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout waterDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> waterDescriptorSets;

    // Uniform buffer for water rendering
    struct WaterUniformBuffer {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec4 cameraPos;
        alignas(16) glm::vec4 shallowColor;
        alignas(16) glm::vec4 deepColor;
        alignas(4) float time;
        alignas(4) float heightScale;
        alignas(4) float gridSize;
        alignas(4) float fresnelPower;
        alignas(4) float reflectionStrength;
        alignas(4) float specularPower;
        alignas(4) float padding1;
        alignas(4) float padding2;
    };
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;

    // Push constants for compute shaders
    struct WaveSimulationPushConstants {
        float deltaTime;
        float waveSpeed;
        float damping;
        int gridSize;
    };

    struct NormalGenerationPushConstants {
        int gridSize;
        float heightScale;
        float texelSize;
        float padding;
    };

    // Ripple injection
    struct RippleBuffer {
        glm::vec4 ripples[16];  // xy = position, z = strength, w = radius
        int rippleCount;
        int padding[3];
    };
    VkBuffer rippleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory rippleBufferMemory = VK_NULL_HANDLE;
    void* rippleBufferMapped = nullptr;
    std::vector<RipplePoint> pendingRipples;

    // Descriptor pool for water system
    VkDescriptorPool waterDescriptorPool = VK_NULL_HANDLE;

    // Resource creation methods
    void createHeightMaps();
    void createNormalMap();
    void createSamplers();
    void createWaterMesh();
    void createUniformBuffers();
    void createRippleBuffer();
    void createDescriptorPool();

    // Pipeline creation methods
    void createComputeDescriptorSetLayouts();
    void createComputePipelines();
    void createGraphicsDescriptorSetLayout();
    void createGraphicsPipeline();
    void createDescriptorSets();

    // Simulation methods
    void dispatchWaveSimulation(VkCommandBuffer commandBuffer, float deltaTime, uint32_t frameIndex);
    void dispatchNormalGeneration(VkCommandBuffer commandBuffer, uint32_t frameIndex);
    void injectRipples();
    void swapHeightMaps();

    // Helper methods
    void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
};
