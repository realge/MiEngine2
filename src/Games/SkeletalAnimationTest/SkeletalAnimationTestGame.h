#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "loader/ModelLoader.h"
#include "animation/SkeletalMeshComponent.h"
#include "animation/AnimationClip.h"
#include <iostream>
#include <vector>
#include <string>

class SkeletalAnimationTestGame : public Game {
public:
    void OnInit() override {
        std::cout << "Skeletal Animation Test Mode Initialized" << std::endl;
        std::cout << "Controls:" << std::endl;
        std::cout << "  WASD - Move camera" << std::endl;
        std::cout << "  Mouse - Look around" << std::endl;
        std::cout << "  1-9 - Switch animations (if available)" << std::endl;
        std::cout << "  P - Pause/Resume animation" << std::endl;
        std::cout << "  R - Reset animation to start" << std::endl;
        std::cout << "  +/- - Increase/Decrease playback speed" << std::endl;
        std::cout << "  L - Toggle animation looping" << std::endl;

        // Setup Scene lighting
        if (m_Scene) {
            m_Scene->clearLights();

            // Main directional light from above-front
            m_Scene->addLight(
                glm::vec3(0.3f, -0.8f, 0.5f),     // Direction
                glm::vec3(1.0f, 0.98f, 0.95f),    // Warm white
                1.5f,                              // Intensity
                0.0f,                              // Radius (0 for directional)
                1.0f,                              // Falloff
                true                               // isDirectional
            );

            // Fill light from the side
            m_Scene->addLight(
                glm::vec3(-0.5f, -0.3f, -0.5f),
                glm::vec3(0.6f, 0.7f, 1.0f),      // Cool blue
                0.5f,
                0.0f,
                1.0f,
                true
            );
        }

        // Setup Camera
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(0.0f, 2.0f, 5.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 1.0f, 0.0f));
            m_Camera->setFarPlane(100.0f);
            m_Camera->setFOV(60.0f);
        }

        // Create the test scene
        CreateTestScene();
    }

    void OnUpdate(float deltaTime) override {
        HandleInput();

        // Update animation info for UI display
        UpdateAnimationInfo();
    }

    void OnRender() override {
        // Render animation debug info using ImGui
        RenderDebugUI();
    }

    void OnShutdown() override {
        std::cout << "Skeletal Animation Test Mode Shutdown" << std::endl;
    }

private:
    // Animation control state
    bool m_IsPaused = false;
    bool m_IsLooping = true;
    float m_PlaybackSpeed = 1.0f;
    int m_CurrentAnimationIndex = 0;
    size_t m_SkeletalInstanceIndex = 0;

    // Animation info for display
    std::string m_CurrentAnimationName = "None";
    float m_CurrentTime = 0.0f;
    float m_Duration = 0.0f;
    int m_BoneCount = 0;

    void HandleInput() {
        // These would need Input system integration
        // For now, we'll just document the intended controls
    }

    void UpdateAnimationInfo() {
        if (!m_Scene) return;

        const auto& instances = m_Scene->getMeshInstances();
        if (m_SkeletalInstanceIndex >= instances.size()) return;

        const auto& instance = instances[m_SkeletalInstanceIndex];
        if (!instance.skeletalMesh) return;

        auto* skelComp = instance.skeletalMesh.get();
        m_CurrentTime = skelComp->getCurrentTime();
        m_IsPaused = !skelComp->isPlaying();
        m_PlaybackSpeed = skelComp->getPlaybackSpeed();
        m_IsLooping = skelComp->isLooping();
        m_BoneCount = static_cast<int>(skelComp->getBoneCount());

        // Get animation name if available
        // For now just show index
        m_CurrentAnimationName = "Animation " + std::to_string(m_CurrentAnimationIndex);
    }

    void RenderDebugUI() {
        // This would use ImGui - integrate with debug UI system
        // For now, console output on significant changes
    }

    void CreateTestScene() {
        if (!m_Scene) return;

        ModelLoader modelLoader;

        // Create ground plane
        auto groundMaterial = std::make_shared<Material>();
        groundMaterial->diffuseColor = glm::vec3(0.3f, 0.3f, 0.35f);
        groundMaterial->setPBRProperties(0.0f, 0.8f);
        groundMaterial->alpha = 1.0f;

        groundMaterial->setTexture(TextureType::Diffuse, nullptr);
        groundMaterial->setTexture(TextureType::Normal, nullptr);
        groundMaterial->setTexture(TextureType::MetallicRoughness, nullptr);
        groundMaterial->setTexture(TextureType::Emissive, nullptr);
        groundMaterial->setTexture(TextureType::AmbientOcclusion, nullptr);

        VkDescriptorSet groundDescriptorSet = m_Scene->getRenderer()->createMaterialDescriptorSet(*groundMaterial);
        if (groundDescriptorSet != VK_NULL_HANDLE) {
            groundMaterial->setDescriptorSet(groundDescriptorSet);
        }

        Transform groundTransform;
        groundTransform.position = glm::vec3(0.0f, 0.0f, 0.0f);
        groundTransform.scale = glm::vec3(20.0f, 1.0f, 20.0f);

        MeshData groundPlane = modelLoader.CreatePlane(1.0f, 1.0f);
        std::vector<MeshData> groundMeshes = { groundPlane };
        m_Scene->createMeshesFromData(groundMeshes, groundTransform, groundMaterial);

        // Try to load a skeletal model
        // First, check if we have any FBX files with animations
        std::vector<std::string> testModels = {
            "models/anim.fbx",
              // Your existing model
        };

        bool modelLoaded = false;
        for (const auto& modelPath : testModels) {
            Transform modelTransform;
            modelTransform.position = glm::vec3(0.0f, 0.0f, 0.0f);
            modelTransform.scale = glm::vec3(1.0f);

            if (m_Scene->loadSkeletalModel(modelPath, modelTransform)) {
                std::cout << "Loaded skeletal model: " << modelPath << std::endl;
                m_SkeletalInstanceIndex = m_Scene->getMeshInstances().size() - 1;
                modelLoaded = true;
                break;
            }
        }

        if (!modelLoaded) {
            std::cout << "No skeletal models found. Creating a test skeleton visualization..." << std::endl;
            CreateFallbackTestScene();
        }

        // Add some reference objects to help visualize scale
        CreateReferenceObjects(modelLoader);

        // Add a reference sphere next to the skeletal model to compare lighting
        CreateReferenceSphere(modelLoader);

        std::cout << "Skeletal Animation Test Scene Created" << std::endl;
        std::cout << "Total mesh instances: " << m_Scene->getMeshInstances().size() << std::endl;
    }

    void CreateFallbackTestScene() {
        // If no skeletal model is available, create some static objects
        // to at least verify the scene is rendering correctly
        if (!m_Scene) return;

        ModelLoader modelLoader;

        // Create a humanoid-like arrangement of spheres to represent a skeleton
        struct JointSphere {
            glm::vec3 position;
            float scale;
            glm::vec3 color;
        };

        std::vector<JointSphere> joints = {
            // Spine
            { glm::vec3(0.0f, 0.5f, 0.0f), 0.15f, glm::vec3(1.0f, 1.0f, 1.0f) },   // Pelvis
            { glm::vec3(0.0f, 0.8f, 0.0f), 0.12f, glm::vec3(1.0f, 1.0f, 1.0f) },   // Spine1
            { glm::vec3(0.0f, 1.1f, 0.0f), 0.12f, glm::vec3(1.0f, 1.0f, 1.0f) },   // Spine2
            { glm::vec3(0.0f, 1.4f, 0.0f), 0.15f, glm::vec3(1.0f, 1.0f, 1.0f) },   // Chest
            { glm::vec3(0.0f, 1.7f, 0.0f), 0.2f, glm::vec3(1.0f, 0.8f, 0.7f) },    // Head

            // Left arm
            { glm::vec3(-0.3f, 1.35f, 0.0f), 0.1f, glm::vec3(0.2f, 0.6f, 1.0f) },  // L Shoulder
            { glm::vec3(-0.55f, 1.1f, 0.0f), 0.08f, glm::vec3(0.2f, 0.6f, 1.0f) }, // L Elbow
            { glm::vec3(-0.75f, 0.85f, 0.0f), 0.1f, glm::vec3(0.2f, 0.6f, 1.0f) }, // L Hand

            // Right arm
            { glm::vec3(0.3f, 1.35f, 0.0f), 0.1f, glm::vec3(1.0f, 0.3f, 0.3f) },   // R Shoulder
            { glm::vec3(0.55f, 1.1f, 0.0f), 0.08f, glm::vec3(1.0f, 0.3f, 0.3f) },  // R Elbow
            { glm::vec3(0.75f, 0.85f, 0.0f), 0.1f, glm::vec3(1.0f, 0.3f, 0.3f) },  // R Hand

            // Left leg
            { glm::vec3(-0.15f, 0.45f, 0.0f), 0.1f, glm::vec3(0.2f, 1.0f, 0.3f) }, // L Hip
            { glm::vec3(-0.15f, 0.25f, 0.0f), 0.08f, glm::vec3(0.2f, 1.0f, 0.3f) },// L Knee
            { glm::vec3(-0.15f, 0.05f, 0.0f), 0.1f, glm::vec3(0.2f, 1.0f, 0.3f) }, // L Foot

            // Right leg
            { glm::vec3(0.15f, 0.45f, 0.0f), 0.1f, glm::vec3(1.0f, 1.0f, 0.2f) },  // R Hip
            { glm::vec3(0.15f, 0.25f, 0.0f), 0.08f, glm::vec3(1.0f, 1.0f, 0.2f) }, // R Knee
            { glm::vec3(0.15f, 0.05f, 0.0f), 0.1f, glm::vec3(1.0f, 1.0f, 0.2f) },  // R Foot
        };

        for (const auto& joint : joints) {
            auto material = std::make_shared<Material>();
            material->diffuseColor = joint.color;
            material->setPBRProperties(0.0f, 0.4f);
            material->alpha = 1.0f;

            material->setTexture(TextureType::Diffuse, nullptr);
            material->setTexture(TextureType::Normal, nullptr);
            material->setTexture(TextureType::MetallicRoughness, nullptr);
            material->setTexture(TextureType::Emissive, nullptr);
            material->setTexture(TextureType::AmbientOcclusion, nullptr);

            VkDescriptorSet matDescriptor = m_Scene->getRenderer()->createMaterialDescriptorSet(*material);
            if (matDescriptor != VK_NULL_HANDLE) {
                material->setDescriptorSet(matDescriptor);
            }

            Transform transform;
            transform.position = joint.position;
            transform.scale = glm::vec3(joint.scale);

            MeshData sphereData = modelLoader.CreateSphere(1.0f, 16, 16);
            std::vector<MeshData> meshes = { sphereData };
            m_Scene->createMeshesFromData(meshes, transform, material);
        }

        std::cout << "Created fallback skeleton visualization (no animated model found)" << std::endl;
        std::cout << "Place an FBX file with skeletal animation in the models/ folder" << std::endl;
    }

    void CreateReferenceSphere(ModelLoader& modelLoader) {
        if (!m_Scene) return;

        // Create a white sphere to compare lighting with skeletal mesh
        auto sphereMaterial = std::make_shared<Material>();
        sphereMaterial->diffuseColor = glm::vec3(0.9f, 0.9f, 0.9f);  // White
        sphereMaterial->setPBRProperties(0.0f, 0.5f);  // Non-metallic, medium roughness
        sphereMaterial->alpha = 1.0f;

        sphereMaterial->setTexture(TextureType::Diffuse, nullptr);
        sphereMaterial->setTexture(TextureType::Normal, nullptr);
        sphereMaterial->setTexture(TextureType::MetallicRoughness, nullptr);
        sphereMaterial->setTexture(TextureType::Emissive, nullptr);
        sphereMaterial->setTexture(TextureType::AmbientOcclusion, nullptr);

        VkDescriptorSet sphereDescriptor = m_Scene->getRenderer()->createMaterialDescriptorSet(*sphereMaterial);
        if (sphereDescriptor != VK_NULL_HANDLE) {
            sphereMaterial->setDescriptorSet(sphereDescriptor);
        }

        Transform sphereTransform;
        sphereTransform.position = glm::vec3(2.0f, 1.0f, 0.0f);  // Position to the right of the model
        sphereTransform.scale = glm::vec3(0.5f);  // 0.5m radius sphere

        MeshData sphereData = modelLoader.CreateSphere(1.0f, 32, 32);
        std::vector<MeshData> meshes = { sphereData };
        m_Scene->createMeshesFromData(meshes, sphereTransform, sphereMaterial);

        std::cout << "Added reference sphere at (2, 1, 0)" << std::endl;
    }

    void CreateReferenceObjects(ModelLoader& modelLoader) {
        // Create axis indicators at origin
        struct AxisIndicator {
            glm::vec3 position;
            glm::vec3 scale;
            glm::vec3 color;
        };

        std::vector<AxisIndicator> axes = {
            { glm::vec3(1.0f, 0.01f, 0.0f), glm::vec3(2.0f, 0.02f, 0.02f), glm::vec3(1.0f, 0.0f, 0.0f) }, // X axis (red)
            { glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.02f, 2.0f, 0.02f), glm::vec3(0.0f, 1.0f, 0.0f) },  // Y axis (green)
            { glm::vec3(0.0f, 0.01f, 1.0f), glm::vec3(0.02f, 0.02f, 2.0f), glm::vec3(0.0f, 0.0f, 1.0f) }, // Z axis (blue)
        };

        for (const auto& axis : axes) {
            auto material = std::make_shared<Material>();
            material->diffuseColor = axis.color;
            material->setPBRProperties(0.0f, 0.5f);
            material->emissiveColor = axis.color * 0.5f;
            material->emissiveStrength = 0.5f;
            material->alpha = 1.0f;

            material->setTexture(TextureType::Diffuse, nullptr);
            material->setTexture(TextureType::Normal, nullptr);
            material->setTexture(TextureType::MetallicRoughness, nullptr);
            material->setTexture(TextureType::Emissive, nullptr);
            material->setTexture(TextureType::AmbientOcclusion, nullptr);

            VkDescriptorSet matDescriptor = m_Scene->getRenderer()->createMaterialDescriptorSet(*material);
            if (matDescriptor != VK_NULL_HANDLE) {
                material->setDescriptorSet(matDescriptor);
            }

            Transform transform;
            transform.position = axis.position;
            transform.scale = axis.scale;

            MeshData cubeData = modelLoader.CreateCube(1.0f);
            std::vector<MeshData> meshes = { cubeData };
            m_Scene->createMeshesFromData(meshes, transform, material);
        }
    }
};
