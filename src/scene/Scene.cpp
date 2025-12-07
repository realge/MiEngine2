#include "scene/Scene.h"
#include "VulkanRenderer.h"
#include <iostream>
#include <filesystem>


Scene::Scene(VulkanRenderer* renderer) : renderer(renderer) {
    // Link physics world to this scene
    m_PhysicsWorld.setScene(this);
}

Scene::~Scene() {
    meshInstances.clear();
    textureCache.clear();
}

bool Scene::loadModel(const std::string& filename, const Transform& transform) {
    if (!modelLoader.LoadModel(filename)) {
        std::cerr << "Failed to load model: " << filename << std::endl;
        return false;
    }
    
    const std::vector<MeshData>& meshDataList = modelLoader.GetMeshData();
    if (meshDataList.empty()) {
        std::cerr << "No meshes found in model: " << filename << std::endl;
        return false;
    }
    
    createMeshesFromData(meshDataList, transform);
    return true;
}

void Scene::createMeshesFromData(const std::vector<MeshData>& meshDataList, 
                                 const Transform& transform) {//overloaded
    // Create a default material
    auto defaultMaterial = std::make_shared<Material>();
    
    // Create descriptor set for the default material
    if (renderer) {
        VkDescriptorSet ds = renderer->createMaterialDescriptorSet(*defaultMaterial);
        if (ds != VK_NULL_HANDLE) {
            defaultMaterial->setDescriptorSet(ds);
        } else {
            std::cerr << "Failed to create descriptor set for default material in createMeshesFromData" << std::endl;
        }
    }

    // Call the full version of the method
    createMeshesFromData(meshDataList, transform, defaultMaterial);
}

// In Scene.cpp, modify loadTexturedModel to add better error checking:

bool Scene::loadTexturedModel(const std::string& modelFilename, const std::string& textureFilename, 
                             const Transform& transform) {
    if (!modelLoader.LoadModel(modelFilename)) {
        std::cerr << "Failed to load model: " << modelFilename << std::endl;
        return false;
    }
    
    const std::vector<MeshData>& meshDataList = modelLoader.GetMeshData();
    if (meshDataList.empty()) {
        std::cerr << "No meshes found in model: " << modelFilename << std::endl;
        return false;
    }
    
    // Create material first
    auto myMaterial = std::make_shared<Material>();
    
    // Load texture if filename provided
    std::shared_ptr<Texture> texture = nullptr;
if (!textureFilename.empty()) {
    std::cout << "Loading texture: " << textureFilename << std::endl;
    
    // Check if file exists
    if (!std::filesystem::exists(textureFilename)) {
        std::cerr << "ERROR: Texture file does not exist: " << textureFilename << std::endl;
    } else {
        texture = loadTexture(textureFilename);
        if (!texture) {
            std::cerr << "Failed to load texture: " << textureFilename << std::endl;
            std::cerr << "Using default white texture instead." << std::endl;
        } else {
            std::cout << "Texture loaded successfully: " << textureFilename << std::endl;
            
            // IMPORTANT: Set the texture on the material
            myMaterial->setTexture(TextureType::Diffuse, texture);
            std::cout << "Texture set on material as Diffuse map" << std::endl;
        }
    }
}
    
    // Set default material properties
    myMaterial->diffuseColor = glm::vec3(1.0f, 1.0f, 1.0f); // White base color
    myMaterial->metallic = 0.0f;  // Non-metallic
    myMaterial->roughness = 0.8f; // Slightly rough
    myMaterial->alpha = 1.0f;     // Fully opaque
    
    // Create descriptor set for the material
    VkDescriptorSet materialDescriptorSet = renderer->createMaterialDescriptorSet(*myMaterial);
    if (materialDescriptorSet == VK_NULL_HANDLE) {
        std::cerr << "Failed to create material descriptor set!" << std::endl;
        return false;
    }
    myMaterial->setDescriptorSet(materialDescriptorSet);
    std::cout << "Material descriptor set created and assigned" << std::endl;
    
    // Create meshes with the material
    createMeshesFromData(meshDataList, transform, myMaterial);
    
    std::cout << "Model loaded with " << meshDataList.size() << " mesh(es)" << std::endl;
    return true;
}


bool Scene::loadTexturedModelPBR(const std::string& modelFilename, 
                               const MaterialTexturePaths& texturePaths,
                               const Transform& transform) {
    if (!modelLoader.LoadModel(modelFilename)) {
        std::cerr << "Failed to load model: " << modelFilename << std::endl;
        return false;
    }
    
    const std::vector<MeshData>& meshDataList = modelLoader.GetMeshData();
    if (meshDataList.empty()) {
        std::cerr << "No meshes found in model: " << modelFilename << std::endl;
        return false;
    }
    
    // Create material with multiple textures
    auto material = std::make_shared<Material>();
    
    // Load textures
    if (!texturePaths.diffuse.empty()) {
        auto texture = loadTexture(texturePaths.diffuse);
        if (texture) material->setTexture(TextureType::Diffuse, texture);
    }
    
    if (!texturePaths.normal.empty()) {
        auto texture = loadTexture(texturePaths.normal);
        if (texture) material->setTexture(TextureType::Normal, texture);
    }
    
    if (!texturePaths.metallic.empty()) {
        auto texture = loadTexture(texturePaths.metallic);
        if (texture) material->setTexture(TextureType::Metallic, texture);
    }
    
    if (!texturePaths.roughness.empty()) {
        auto texture = loadTexture(texturePaths.roughness);
        if (texture) material->setTexture(TextureType::Roughness, texture);
    }
    
    if (!texturePaths.ambientOcclusion.empty()) {
        auto texture = loadTexture(texturePaths.ambientOcclusion);
        if (texture) material->setTexture(TextureType::AmbientOcclusion, texture);
    }
    
    if (!texturePaths.emissive.empty()) {
        auto texture = loadTexture(texturePaths.emissive);
        if (texture) material->setTexture(TextureType::Emissive, texture);
    }

    // Handle metallic/roughness textures
    std::shared_ptr<Texture> metallicTex = nullptr;
    std::shared_ptr<Texture> roughnessTex = nullptr;

    if (!texturePaths.metallic.empty()) {
        metallicTex = loadTexture(texturePaths.metallic);
    }
    
    if (!texturePaths.roughness.empty()) {
        roughnessTex = loadTexture(texturePaths.roughness);
    }

    // If we have separate textures, combine them
    if (metallicTex || roughnessTex) {
        // If we have both, or just one of them, we need to ensure they are combined
        // or at least put into the correct slot if they are already combined (not handled here but good to note)
        
        // Check if we already have a combined texture provided (unlikely in this path but possible)
        // For now, assume we need to combine if we have separate paths
        
        std::shared_ptr<Texture> combinedTex = TextureUtils::combineMetallicRoughness(
            renderer->getDevice(),
            renderer->getPhysicalDevice(),
            renderer->getCommandPool(),
            renderer->getGraphicsQueue(),
            metallicTex,
            roughnessTex
        );
        
        if (combinedTex) {
            material->setTexture(TextureType::MetallicRoughness, combinedTex);
            std::cout << "Combined metallic/roughness textures for " << modelFilename << std::endl;
        }
    }
    
    // Set default scalar properties (will be used if textures are missing or as multipliers)
    material->diffuseColor = glm::vec3(1.0f);
    material->metallic = 1.0f; 
    material->roughness = 1.0f;
    material->alpha = 1.0f; 
    
    // Create descriptor set for the material
    VkDescriptorSet materialDescriptorSet = renderer->createMaterialDescriptorSet(*material);
    if (materialDescriptorSet == VK_NULL_HANDLE) {
        std::cerr << "Failed to create material descriptor set!" << std::endl;
        return false;
    }
    material->setDescriptorSet(materialDescriptorSet);
    
    // Create meshes with the material
    createMeshesFromData(meshDataList, transform, material);
    
    std::cout << "PBR Model loaded: " << modelFilename << std::endl;
    return true;
}

Material Scene::createMaterialWithTextures(const MaterialTexturePaths& texturePaths) {
    Material material;
    
    // Load diffuse/albedo texture if provided
    if (!texturePaths.diffuse.empty()) {
        auto texture = loadTexture(texturePaths.diffuse);
        if (texture) {
            material.setTexture(TextureType::Diffuse, texture);
        }
    }
    
    // Load normal map if provided
    if (!texturePaths.normal.empty()) {
        auto texture = loadTexture(texturePaths.normal);
        if (texture) {
            material.setTexture(TextureType::Normal, texture);
            
        }
    }
    
    // Load metallic map if provided
    if (!texturePaths.metallic.empty()) {
        auto texture = loadTexture(texturePaths.metallic);
        if (texture) {
            material.setTexture(TextureType::Metallic, texture);
        }
    }
    
    // Load roughness map if provided
    if (!texturePaths.roughness.empty()) {
        auto texture = loadTexture(texturePaths.roughness);
        if (texture) {
            material.setTexture(TextureType::Roughness, texture);
        }
    }
    
    // Load ambient occlusion map if provided
    if (!texturePaths.ambientOcclusion.empty()) {
        auto texture = loadTexture(texturePaths.ambientOcclusion);
        if (texture) {
            material.setTexture(TextureType::AmbientOcclusion, texture);
        }
    }
    
    // Load emissive map if provided
    if (!texturePaths.emissive.empty()) {
        auto texture = loadTexture(texturePaths.emissive);
        if (texture) {
            material.setTexture(TextureType::Emissive, texture);
        }
    }
    
    // Load height/displacement map if provided
    if (!texturePaths.height.empty()) {
        auto texture = loadTexture(texturePaths.height);
        if (texture) {
            material.setTexture(TextureType::Height, texture);
        }
    }
    
    // Load specular map if provided
    if (!texturePaths.specular.empty()) {
        auto texture = loadTexture(texturePaths.specular);
        if (texture) {
            material.setTexture(TextureType::Specular, texture);
        }
    }
    
    return material;
}

std::shared_ptr<Texture> Scene::loadTexture(const std::string& filename) {
    // Check if file exists
    if (!std::filesystem::exists(filename)) {
        std::cerr << "Texture file does not exist: " << filename << std::endl;
        return nullptr;
    }
    
    // Check if texture is already loaded
    auto it = textureCache.find(filename);
    if (it != textureCache.end()) {
        return it->second;
    }
    
    // Create new texture
    auto texture = std::make_shared<Texture>(renderer->getDevice(), renderer->getPhysicalDevice());
    
    // Load texture from file
    if (!texture->loadFromFile(filename, renderer->getCommandPool(), renderer->getGraphicsQueue())) {
        std::cerr << "Failed to load texture from file: " << filename << std::endl;
        return nullptr;
    }
    
    // Cache the texture
    textureCache[filename] = texture;
    
    return texture;
}

void Scene::createMeshesFromData(const std::vector<MeshData>& meshDataList, const Transform& transform,
                               const std::shared_ptr<Material>& material) {
    for (const auto& meshData : meshDataList) {
        // Create a new mesh with the provided material (shared pointer)
        auto mesh = std::make_shared<Mesh>(renderer->getDevice(), renderer->getPhysicalDevice(), 
                                        meshData, material);
        mesh->createBuffers(renderer->getCommandPool(), renderer->getGraphicsQueue());
        
        // Create an instance of this mesh
        meshInstances.emplace_back(mesh, transform);
    }
}

void Scene::addMeshInstance(std::shared_ptr<Mesh> mesh, const Transform& transform) {
    meshInstances.emplace_back(mesh, transform);
}

void Scene::update(float deltaTime) {
    // Update physics simulation
    m_PhysicsWorld.update(deltaTime);

    // Update skeletal animations
    for (auto& instance : meshInstances) {
        if (instance.skeletalMesh) {
            instance.skeletalMesh->update(deltaTime);
        }
    }
}

void Scene::enablePhysics(size_t instanceIndex, RigidBodyType bodyType) {
    if (instanceIndex >= meshInstances.size()) {
        return;
    }

    MeshInstance* instance = &meshInstances[instanceIndex];

    // Assign unique ID if not already set
    if (instance->instanceId == 0) {
        instance->instanceId = m_NextInstanceId++;
    }

    // Create RigidBody component
    instance->rigidBody = std::make_shared<RigidBodyComponent>();
    instance->rigidBody->ownerIndex = static_cast<uint32_t>(instanceIndex);
    instance->rigidBody->bodyType = bodyType;

    // Set inverse mass based on body type
    if (bodyType == RigidBodyType::Dynamic) {
        instance->rigidBody->inverseMass = 1.0f / instance->rigidBody->mass;
    } else {
        instance->rigidBody->inverseMass = 0.0f; // Static/kinematic have infinite mass
    }

    // Create Collider component and initialize from mesh AABB
    instance->collider = std::make_shared<ColliderComponent>();
    instance->collider->ownerIndex = static_cast<uint32_t>(instanceIndex);
    instance->collider->initFromMeshAABB(this);
}

void Scene::enablePhysics(MeshInstance* instance, RigidBodyType bodyType) {
    if (!instance) {
        return;
    }

    // Find the index of this instance in the vector
    size_t instanceIndex = INVALID_OWNER_INDEX;
    for (size_t i = 0; i < meshInstances.size(); i++) {
        if (&meshInstances[i] == instance) {
            instanceIndex = i;
            break;
        }
    }

    if (instanceIndex == INVALID_OWNER_INDEX) {
        return; // Instance not found in scene
    }

    // Assign unique ID if not already set
    if (instance->instanceId == 0) {
        instance->instanceId = m_NextInstanceId++;
    }

    // Create RigidBody component
    instance->rigidBody = std::make_shared<RigidBodyComponent>();
    instance->rigidBody->ownerIndex = static_cast<uint32_t>(instanceIndex);
    instance->rigidBody->bodyType = bodyType;

    // Set inverse mass based on body type
    if (bodyType == RigidBodyType::Dynamic) {
        instance->rigidBody->inverseMass = 1.0f / instance->rigidBody->mass;
    } else {
        instance->rigidBody->inverseMass = 0.0f; // Static/kinematic have infinite mass
    }

    // Create Collider component and initialize from mesh AABB
    instance->collider = std::make_shared<ColliderComponent>();
    instance->collider->ownerIndex = static_cast<uint32_t>(instanceIndex);
    instance->collider->initFromMeshAABB(this);
}

void Scene::draw(VkCommandBuffer commandBuffer, const glm::mat4& view, const glm::mat4& proj, uint32_t frameIndex) {
    // Common setup - update view/projection matrices
    renderer->updateViewProjection(view, proj);

    // Check which pipeline to use
    bool usePBR = renderer->getRenderMode() == RenderMode::PBR ||
                 renderer->getRenderMode() == RenderMode::PBR_IBL;

    // Track which pipeline is currently bound
    enum class BoundPipeline { None, Standard, PBR, Skeletal };
    BoundPipeline currentPipeline = BoundPipeline::None;

    // Draw each mesh instance
    for (auto& instance : meshInstances) {
        // Get the model matrix for this instance
        glm::mat4 model = instance.transform.getModelMatrix();

        // Check if this is a skeletal mesh
        if (instance.isSkeletal && instance.skeletalMesh && renderer->isSkeletalPipelineReady()) {
            // Use skeletal pipeline
            // Descriptor set layout: Set 0=MVP, Set 1=Material, Set 2=Light, Set 3=IBL, Set 4=Bones
            if (currentPipeline != BoundPipeline::Skeletal) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->getSkeletalPipeline());

                // Bind MVP descriptor set (set 0)
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderer->getSkeletalPipelineLayout(),
                    0,  // Set index 0
                    1,  // One descriptor set
                    &renderer->getMVPDescriptorSets()[frameIndex],
                    0, nullptr
                );

                // Bind light descriptor set (set 2)
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderer->getSkeletalPipelineLayout(),
                    2,  // Set index 2
                    1,  // One descriptor set
                    &renderer->getLightDescriptorSets()[frameIndex],
                    0, nullptr
                );

                // Bind IBL descriptor set (set 3)
                if (renderer->getIBLSystem() && renderer->getIBLSystem()->isReady()) {
                     vkCmdBindDescriptorSets(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        renderer->getSkeletalPipelineLayout(),
                        3,  // Set index 3
                        1,  // One descriptor set
                        &renderer->getIBLSystem()->getDescriptorSets()[frameIndex],
                        0, nullptr
                    );
                }

                currentPipeline = BoundPipeline::Skeletal;
            }

            // Ensure skeletal instance resources are created
            renderer->createSkeletalInstanceResources(instance.instanceId, instance.skeletalMesh->getBoneCount());

            // Update bone matrices for this frame
            const auto& boneMatrices = instance.skeletalMesh->getFinalBoneMatrices();
            renderer->updateBoneMatrices(instance.instanceId, boneMatrices, frameIndex);

            // Bind bone matrix descriptor set (set 4)
            VkDescriptorSet boneDescriptorSet = renderer->getBoneMatrixDescriptorSet(instance.instanceId, frameIndex);
            if (boneDescriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderer->getSkeletalPipelineLayout(),
                    4,  // Set index 4 (bone matrices)
                    1,
                    &boneDescriptorSet,
                    0, nullptr
                );
            }

            // Push the model matrix as a push constant
            PushConstant pushConstant = renderer->createPushConstant(model, *instance.mesh->getMaterial());
            vkCmdPushConstants(
                commandBuffer,
                renderer->getSkeletalPipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstant),
                &pushConstant
            );

            // Bind material descriptor set (set 1)
            VkDescriptorSet materialDescriptorSet = instance.mesh->getMaterial()->getDescriptorSet();
            if (materialDescriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderer->getSkeletalPipelineLayout(),
                    1,  // Set index 1 (material)
                    1,
                    &materialDescriptorSet,
                    0, nullptr
                );
            }

            // Draw the skeletal mesh
            instance.mesh->bind(commandBuffer);
            instance.mesh->draw(commandBuffer);
            renderer->addDrawCall(0, instance.mesh->indexCount);

        } else if (usePBR) {
            // Use PBR pipeline for non-skeletal meshes
            if (currentPipeline != BoundPipeline::PBR) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->getPBRPipeline());

                // Bind MVP descriptor set (set 0)
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderer->getPBRPipelineLayout(),
                    0,  // Set index 0
                    1,  // One descriptor set
                    &renderer->getMVPDescriptorSets()[frameIndex],
                    0, nullptr
                );

                // Bind light descriptor set (set 2)
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderer->getPBRPipelineLayout(),
                    2,  // Set index 2
                    1,  // One descriptor set
                    &renderer->getLightDescriptorSets()[frameIndex],
                    0, nullptr
                );

                // Bind IBL descriptor set (set 3)
                if (renderer->getIBLSystem() && renderer->getIBLSystem()->isReady()) {
                     vkCmdBindDescriptorSets(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        renderer->getPBRPipelineLayout(),
                        3,  // Set index 3
                        1,  // One descriptor set
                        &renderer->getIBLSystem()->getDescriptorSets()[frameIndex],
                        0, nullptr
                    );
                }

                currentPipeline = BoundPipeline::PBR;
            }

            // Push the model matrix as a push constant
            PushConstant pushConstant = renderer->createPushConstant(model, *instance.mesh->getMaterial());
            vkCmdPushConstants(
                commandBuffer,
                renderer->getPBRPipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstant),
                &pushConstant
            );

            // Bind material descriptor set (set 1)
            VkDescriptorSet materialDescriptorSet = instance.mesh->getMaterial()->getDescriptorSet();
            if (materialDescriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderer->getPBRPipelineLayout(),
                    1,  // Set index 1
                    1,
                    &materialDescriptorSet,
                    0, nullptr
                );
            }

            // Draw the mesh
            instance.mesh->bind(commandBuffer);
            instance.mesh->draw(commandBuffer);
            renderer->addDrawCall(0, instance.mesh->indexCount);

        } else {
            // Use standard pipeline
            if (currentPipeline != BoundPipeline::Standard) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->getGraphicsPipeline());

                // Bind MVP descriptor set
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderer->getPipelineLayout(),
                    0,  // Set index 0
                    1,  // One descriptor set
                    &renderer->getMVPDescriptorSets()[frameIndex],
                    0, nullptr
                );

                currentPipeline = BoundPipeline::Standard;
            }

            // Push the model matrix as a push constant
            PushConstant pushConstant = renderer->createPushConstant(model, *instance.mesh->getMaterial());
            vkCmdPushConstants(
                commandBuffer,
                renderer->getPipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(PushConstant),
                &pushConstant
            );

            // Bind material descriptor set
            VkDescriptorSet materialDescriptorSet = instance.mesh->getMaterial()->getDescriptorSet();
            if (materialDescriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderer->getPipelineLayout(),
                    1,  // Set index 1
                    1,
                    &materialDescriptorSet,
                    0, nullptr
                );
            }

            // Draw the mesh
            instance.mesh->bind(commandBuffer);
            instance.mesh->draw(commandBuffer);
            renderer->addDrawCall(0, instance.mesh->indexCount);
        }
    }
}



void Scene::addLight(const glm::vec3& position, const glm::vec3& color, 
                    float intensity, float radius, float falloff, bool isDirectional) {
    Light light;
    light.position = position;
    light.color = color;
    light.intensity = intensity;
    light.radius = radius;
    light.falloff = falloff;
    light.isDirectional = isDirectional;
    
    lights.push_back(light);
}


void Scene::clearMeshInstances() {
    meshInstances.clear();
}


void Scene::removeLight(size_t index) {
    if (index < lights.size()) {
        lights.erase(lights.begin() + index);
    }
}



// In Scene::setupDefaultLighting(), reduce light intensities:

void Scene::setupDefaultLighting() {
    // Clear any existing lights
    clearLights();
    
    // Add a main directional light (sun) with REDUCED intensity
    addLight(
        glm::vec3(1.0f, 1.0f, 1.0f),    // Direction (will be normalized)
        glm::vec3(1.0f, 0.95f, 0.9f),   // Slightly warm white color
        1.0f,                            // Reduced intensity (was 2.0f)
        0.0f,                            // Radius (0 for directional lights)
        1.0f,                            // Falloff (unused for directional)
        true                             // isDirectional = true
    );
    
    // Add a fill light from the opposite direction with REDUCED intensity
    addLight(
        glm::vec3(-0.5f, 0.2f, -0.5f),  // Direction
        glm::vec3(0.6f, 0.7f, 1.0f),    // Slightly blue color
        0.3f,                            // Lower intensity (was 0.5f)
        0.0f,                            // Radius
        1.0f,                            // Falloff
        true                             // isDirectional
    );
    
    // Add a point light with REDUCED intensity
    addLight(
        glm::vec3(2.0f, 1.0f, 2.0f),    // Position
        glm::vec3(1.0f, 0.8f, 0.6f),    // Warm color
        2.0f,                            // Reduced intensity (was 5.0f)
        10.0f,                           // Radius
        2.0f,                            // Falloff
        false                            // isPoint
    );
}

bool Scene::loadPBRModel(
    const std::string& modelFilename,
    const MaterialTexturePaths& texturePaths,
    const glm::vec3& position,
    const glm::vec3& rotation,
    const glm::vec3& scale)
{
    // Create transform
    Transform transform;
    transform.position = position;
    transform.rotation = rotation;
    transform.scale = scale;
    
    // Load model with PBR materials
    std::cout << "Attempting to load PBR model: " << modelFilename << std::endl;
    bool result = loadTexturedModelPBR(modelFilename, texturePaths, transform);
    if (result) {
        std::cout << "Successfully loaded PBR model: " << modelFilename << std::endl;
    } else {
        std::cerr << "FAILED to load PBR model: " << modelFilename << std::endl;
    }
    return result;
}


Material Scene::createPBRMaterial(
    const std::string& albedoPath,
    const std::string& normalPath,
    const std::string& metallicPath,
    const std::string& roughnessPath,
    const std::string& aoPath,
    const std::string& emissivePath,
    float metallic,
    float roughness,
    const glm::vec3& baseColor,
    float emissiveStrength)
{
    Material material;
    
    // Set base color and PBR scalar properties
    material.diffuseColor = baseColor;
    material.setPBRProperties(metallic, roughness);
    material.emissiveStrength = emissiveStrength;
    
    // Load each texture if provided
    std::shared_ptr<Texture> albedoTex = albedoPath.empty() ? nullptr : loadTexture(albedoPath);
    std::shared_ptr<Texture> normalTex = normalPath.empty() ? nullptr : loadTexture(normalPath);
    std::shared_ptr<Texture> metallicTex = metallicPath.empty() ? nullptr : loadTexture(metallicPath);
    std::shared_ptr<Texture> roughnessTex = roughnessPath.empty() ? nullptr : loadTexture(roughnessPath);
    std::shared_ptr<Texture> aoTex = aoPath.empty() ? nullptr : loadTexture(aoPath);
    std::shared_ptr<Texture> emissiveTex = emissivePath.empty() ? nullptr : loadTexture(emissivePath);
    
    // If both metallic and roughness are provided, we could combine them
    std::shared_ptr<Texture> metallicRoughnessTex = nullptr;
    if (metallicTex && roughnessTex) {
        // Try to create a combined texture
        metallicRoughnessTex = TextureUtils::combineMetallicRoughness(
            renderer->getDevice(),
            renderer->getPhysicalDevice(),
            renderer->getCommandPool(),
            renderer->getGraphicsQueue(),
            metallicTex,
            roughnessTex,
            metallic,
            roughness
        );
    } else if (metallicTex) {
        // Just use metallic texture if only that's available
        metallicRoughnessTex = metallicTex;
    } else if (roughnessTex) {
        // Just use roughness texture if only that's available
        metallicRoughnessTex = roughnessTex;
    } else if (metallic >= 0.0f && roughness >= 0.0f) {
        // Create a default texture with neutral values (1.0) so that
        // the scalar uniforms have full control (texture * scalar)
        metallicRoughnessTex = TextureUtils::createDefaultMetallicRoughnessMap(
            renderer->getDevice(),
            renderer->getPhysicalDevice(),
            renderer->getCommandPool(),
            renderer->getGraphicsQueue(),
            1.0f,
            1.0f
        );
    }
    
    // Set textures
    material.setPBRTextures(
        albedoTex,
        normalTex,
        metallicRoughnessTex,     // Will contain combined or individual metallic/roughness
        nullptr,                  // Not needed if we have combined texture
        aoTex,
        emissiveTex
    );
    
    // Create descriptor set
    VkDescriptorSet materialDescriptorSet = renderer->createMaterialDescriptorSet(material);
    if (materialDescriptorSet != VK_NULL_HANDLE) {
        material.setDescriptorSet(materialDescriptorSet);
    } else {
        std::cerr << "Failed to create descriptor set for PBR material" << std::endl;
    }
    
    return material;
}

// Replace the existing setupEnvironment implementation in Scene.cpp
bool Scene::setupEnvironment(const std::string& hdriPath) {
    if (!renderer) {
        std::cerr << "Renderer not initialized" << std::endl;
        return false;
    }
    
    // Set up IBL with the given HDRI environment map
    try {
        bool success = renderer->setupIBL(hdriPath);
        
        if (success) {
            std::cout << "Environment setup successful with HDRI: " << hdriPath << std::endl;
            
            // Switch to PBR_IBL mode if IBL is successfully set up
            renderer->setRenderMode(RenderMode::PBR_IBL);
        } else {
            std::cerr << "Failed to set up environment with HDRI: " << hdriPath << std::endl;
        }
        
        return success;
    } catch (const std::exception& e) {
        std::cerr << "Failed to set up environment: " << e.what() << std::endl;
        return false;
    }
}




const std::vector<MeshInstance>& Scene::getMeshInstances() const {
    return meshInstances;
}


void Scene::clearLights() {
    lights.clear();
}
