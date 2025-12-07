#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "material/Material.h"
#include "mesh/Mesh.h"
#include "loader/ModelLoader.h"
#include "texture/Texture.h"
#include "physics/PhysicsWorld.h"
#include "physics/RigidBodyComponent.h"
#include "physics/ColliderComponent.h"
#include "animation/SkeletalMeshComponent.h"

// Forward declarations
class VulkanRenderer;

namespace MiEngine {
    class Skeleton;
    class AnimationClip;
}

// Struct to hold transform data for each mesh instance
struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    glm::mat4 getModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, scale);
        return model;
    }
};

// Structure to represent texture paths for a material
struct MaterialTexturePaths {
    std::string diffuse;
    std::string normal;
    std::string metallic;
    std::string roughness;
    std::string ambientOcclusion;
    std::string emissive;
    std::string height;
    std::string specular;
};

// Struct to represent a mesh instance in the scene
struct MeshInstance {
    std::shared_ptr<Mesh> mesh;
    Transform transform;

    // Optional physics components (nullptr if not physics-enabled)
    std::shared_ptr<RigidBodyComponent> rigidBody;
    std::shared_ptr<ColliderComponent> collider;

    // Optional skeletal animation component (nullptr if not skeletal mesh)
    std::shared_ptr<MiEngine::SkeletalMeshComponent> skeletalMesh;

    // Unique ID for physics tracking
    uint32_t instanceId = 0;

    // Flag to indicate this is a skeletal mesh (uses different pipeline)
    bool isSkeletal = false;

    MeshInstance(std::shared_ptr<Mesh> m, const Transform& t = Transform())
        : mesh(m), transform(t), rigidBody(nullptr), collider(nullptr),
          skeletalMesh(nullptr), instanceId(0), isSkeletal(false) {}

    // Check if this instance has physics enabled
    bool hasPhysics() const { return rigidBody != nullptr; }
    bool hasCollider() const { return collider != nullptr; }
    bool hasSkeletalAnimation() const { return skeletalMesh != nullptr; }
};

class Scene {
public:
    Scene(VulkanRenderer* renderer);
    ~Scene();

    void createMeshesFromData(const std::vector<MeshData>& meshDataList, 
                          const Transform& transform,
                          const std::shared_ptr<Material>& material);

    // Load a model and add all its meshes to the scene
    bool loadModel(const std::string& filename, const Transform& transform = Transform());
    void createMeshesFromData(const std::vector<MeshData>& meshDataList, const Transform& transform);

    // Load a model with a single texture
    bool loadTexturedModel(const std::string& modelFilename, const std::string& textureFilename, 
                           const Transform& transform = Transform());
    
    // Load a model with multiple textures
    bool loadTexturedModelPBR(const std::string& modelFilename, 
                             const MaterialTexturePaths& texturePaths,
                             const Transform& transform = Transform());
    
    // Add a single mesh instance to the scene
    void addMeshInstance(std::shared_ptr<Mesh> mesh, const Transform& transform = Transform());

    // Skeletal model loading
    bool loadSkeletalModel(const std::string& filename, const Transform& transform = Transform());
    bool loadSkeletalModelPBR(const std::string& modelFilename,
                              const MaterialTexturePaths& texturePaths,
                              const Transform& transform = Transform());

    // Play animation on a skeletal mesh instance
    void playAnimation(size_t instanceIndex, size_t animationIndex, bool loop = true);
    void playAnimation(MeshInstance* instance, std::shared_ptr<MiEngine::AnimationClip> clip, bool loop = true);
    
    // Update all mesh transforms
    void update(float deltaTime);
    
    // Record draw commands for all meshes
    void draw(::VkCommandBuffer_T* commandBuffer, const glm::mat4& view, const glm::mat4& proj, uint32_t frameIndex);


    bool loadPBRModel(
    const std::string& modelFilename,
    const MaterialTexturePaths& texturePaths,
    const glm::vec3& position,
    const glm::vec3& rotation,
    const glm::vec3& scale);
    Material createPBRMaterial(
    const std::string& albedoPath,
    const std::string& normalPath,
    const std::string& metallicPath,
    const std::string& roughnessPath,
    const std::string& aoPath,
    const std::string& emissivePath,
    float metallic,
    float roughness,
    const glm::vec3& baseColor = glm::vec3(1.0f),
    float emissiveStrength = 1.0f);
    bool setupEnvironment(const std::string& hdriPath);
  
    const std::vector<MeshInstance>& getMeshInstances() const;
    MeshInstance* getMeshInstance(size_t index) {
        if (index < meshInstances.size()) return &meshInstances[index];
        return nullptr;
    }

    // Physics support
    PhysicsWorld& getPhysicsWorld() { return m_PhysicsWorld; }
    const PhysicsWorld& getPhysicsWorld() const { return m_PhysicsWorld; }

    // Enable physics on a mesh instance
    void enablePhysics(size_t instanceIndex, RigidBodyType bodyType = RigidBodyType::Dynamic);
    void enablePhysics(MeshInstance* instance, RigidBodyType bodyType = RigidBodyType::Dynamic);

    // Get loaded skeletal model data (for animation access)
    const std::vector<SkeletalModelData>& getSkeletalModels() const { return m_skeletalModels; }

private:
    VulkanRenderer* renderer;// TODO: not sure if this is save
    std::vector<MeshInstance> meshInstances;

    // Storage for loaded textures to prevent duplicates
    std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;

    ModelLoader modelLoader;

    // Physics world
    PhysicsWorld m_PhysicsWorld;
    uint32_t m_NextInstanceId = 1;

    // Skeletal model data storage
    std::vector<SkeletalModelData> m_skeletalModels;

    // Helper to create mesh objects from loaded mesh data
    
    
    // Load or retrieve a cached texture
    std::shared_ptr<Texture> loadTexture(const std::string& filename);
    
    // Create a material with multiple textures
    Material createMaterialWithTextures(const MaterialTexturePaths& texturePaths);

    

public:
    struct Light {
        glm::vec3 position;
        glm::vec3 color;
        float intensity;
        float radius;
        float falloff;
        bool isDirectional;
    };

private:
    std::vector<Light> lights;

public:

    void addLight(const glm::vec3& position, const glm::vec3& color, 
              float intensity = 1.0f, float radius = 10.0f, 
              float falloff = 1.0f, bool isDirectional = false);
    void clearMeshInstances();

    void removeLight(size_t index);
    void setupDefaultLighting();
    void clearLights();
    const std::vector<Light>& getLights() const { return lights; }
    std::vector<Light>& getLights() { return lights; }
   VulkanRenderer* getRenderer() const { return renderer; }
};
