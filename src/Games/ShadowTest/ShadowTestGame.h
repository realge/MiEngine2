#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "loader/ModelLoader.h"
#include <iostream>

class ShadowTestGame : public Game {
public:
    void OnInit() override {
        std::cout << "Shadow Test Mode Initialized" << std::endl;

        // Setup Scene with directional lighting for shadows
        if (m_Scene) {
            m_Scene->clearLights();
            
            // Add a directional light from directly above
            // Direction vector: light travels FROM above DOWN (negative Y)
            m_Scene->addLight(
                glm::vec3(0.0f, -1.0f, 0.0f),     // Straight down - top of sphere should be bright
                glm::vec3(1.0f, 0.98f, 0.95f),    // Slightly warm white color
                1.5f,                              // Intensity
                0.0f,                              // Radius (0 for directional)
                1.0f,                              // Falloff (unused for directional)
                true                               // isDirectional = true
            );
        }

        // Setup Camera for good shadow visibility
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(10.0f, 8.0f, 10.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            m_Camera->setFarPlane(100.0f);
            m_Camera->setFOV(60.0f);
        }

        // Create the test scene geometry
        CreateTestScene();
    }

    void OnUpdate(float deltaTime) override {
        // No special update logic needed
        // Camera movement is handled by default controls
    }

    void OnRender() override {
        // Debug UI is handled by VulkanRenderer
    }

    void OnShutdown() override {
        std::cout << "Shadow Test Mode Shutdown" << std::endl;
    }

private:
    void CreateTestScene() {
        if (!m_Scene) return;

        ModelLoader modelLoader;

        // Create ground plane material (gray, non-metallic, medium roughness)
        auto groundMaterial = std::make_shared<Material>();
        groundMaterial->diffuseColor = glm::vec3(0.5f, 0.5f, 0.5f);
        groundMaterial->setPBRProperties(0.0f, 0.6f); // Low metallic, medium roughness
        groundMaterial->alpha = 1.0f;
        
        // Explicitly set all textures to nullptr to avoid undefined behavior
        groundMaterial->setTexture(TextureType::Diffuse, nullptr);
        groundMaterial->setTexture(TextureType::Normal, nullptr);
        groundMaterial->setTexture(TextureType::MetallicRoughness, nullptr);
        groundMaterial->setTexture(TextureType::Emissive, nullptr);
        groundMaterial->setTexture(TextureType::AmbientOcclusion, nullptr);
        
        // Create descriptor set for the material
        VkDescriptorSet groundDescriptorSet = m_Scene->getRenderer()->createMaterialDescriptorSet(*groundMaterial);
        if (groundDescriptorSet != VK_NULL_HANDLE) {
            groundMaterial->setDescriptorSet(groundDescriptorSet);
        } else {
            std::cerr << "Failed to create descriptor set for ground material!" << std::endl;
        }
        
        // Create ground plane (20x20 units)
        Transform groundTransform;
        groundTransform.position = glm::vec3(0.0f, 0.0f, 0.0f);
        groundTransform.scale = glm::vec3(100.0f, 1.0f, 100.0f); // Scale 1x1 plane to 20x20
        
        MeshData groundPlane = modelLoader.CreatePlane(1.0f, 1.0f);
        std::vector<MeshData> groundMeshes = { groundPlane };
        m_Scene->createMeshesFromData(groundMeshes, groundTransform, groundMaterial);

        // Create test objects (spheres at different heights and positions)
        struct TestObject {
            glm::vec3 position;
            glm::vec3 color;
            float metallic;
            float roughness;
            float scale;
        };

        std::vector<TestObject> objects = {
            // Center sphere - red, non-metallic
            { glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(1.0f, 0.2f, 0.2f), 0.0f, 0.3f, 1.5f },
            
            // Left sphere - blue, slightly metallic
            { glm::vec3(-3.0f, 1.0f, 2.0f), glm::vec3(0.2f, 0.3f, 1.0f), 0.3f, 0.5f, 1.0f },
            
            // Right sphere - green, rough
            { glm::vec3(3.0f, 0.8f, -2.0f), glm::vec3(0.2f, 1.0f, 0.3f), 0.0f, 0.8f, 0.8f },
            
            // Back sphere - yellow, shiny
            { glm::vec3(-1.5f, 2.0f, -3.0f), glm::vec3(1.0f, 0.9f, 0.2f), 0.0f, 0.1f, 1.2f },
            
            // Front sphere - purple, metallic
            { glm::vec3(2.0f, 1.2f, 3.0f), glm::vec3(0.8f, 0.2f, 1.0f), 0.7f, 0.3f, 1.0f }
        };

        for (const auto& obj : objects) {
            auto material = std::make_shared<Material>();
            material->diffuseColor = obj.color;
            material->setPBRProperties(obj.metallic, obj.roughness);
            material->alpha = 1.0f;
            
            // Explicitly set textures to nullptr
            material->setTexture(TextureType::Diffuse, nullptr);
            material->setTexture(TextureType::Normal, nullptr);
            material->setTexture(TextureType::MetallicRoughness, nullptr);
            material->setTexture(TextureType::Emissive, nullptr);
            material->setTexture(TextureType::AmbientOcclusion, nullptr);
            
            // Create descriptor set for the material
            VkDescriptorSet materialDescriptorSet = m_Scene->getRenderer()->createMaterialDescriptorSet(*material);
            if (materialDescriptorSet != VK_NULL_HANDLE) {
                material->setDescriptorSet(materialDescriptorSet);
            } else {
                std::cerr << "Failed to create descriptor set for sphere material!" << std::endl;
            }
            
            Transform transform;
            transform.position = obj.position;
            transform.scale = glm::vec3(obj.scale);
            
            MeshData sphereData = modelLoader.CreateSphere(1.0f, 32, 32);
            std::vector<MeshData> sphereMeshes = { sphereData };
            m_Scene->createMeshesFromData(sphereMeshes, transform, material);
        }

        std::cout << "Created shadow test scene with ground plane and " << objects.size() << " test objects" << std::endl;
    }
};
