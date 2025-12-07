#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "loader/ModelLoader.h"
#include <iostream>

class PointLightTestGame : public Game {
public:
    void OnInit() override {
        std::cout << "Point Light Test Mode Initialized" << std::endl;

        // Setup Scene with point lights
        if (m_Scene) {
            m_Scene->clearLights();
            
            // Add a red point light
            m_Scene->addLight(
                glm::vec3(-5.0f, 2.0f, 0.0f),     // Position
                glm::vec3(1.0f, 0.0f, 0.0f),      // Red color
                50.0f,                             // Intensity
                20.0f,                            // Radius
                1.0f,                             // Falloff
                false                             // isDirectional = false
            );

            // Add a blue point light
            m_Scene->addLight(
                glm::vec3(5.0f, 2.0f, 0.0f),      // Position
                glm::vec3(0.0f, 0.0f, 1.0f),      // Blue color
                50.0f,                             // Intensity
                20.0f,                            // Radius
                1.0f,                             // Falloff
                false                             // isDirectional = false
            );

             // Add a green point light
            m_Scene->addLight(
                glm::vec3(0.0f, 2.0f, 5.0f),      // Position
                glm::vec3(0.0f, 1.0f, 0.0f),      // Green color
                50.0f,                             // Intensity
                20.0f,                            // Radius
                1.0f,                             // Falloff
                false                             // isDirectional = false
            );
        }

        // Setup Camera
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(0.0f, 10.0f, 15.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            m_Camera->setFarPlane(100.0f);
            m_Camera->setFOV(60.0f);
        }

        // Create the test scene geometry
        CreateTestScene();
    }

    void OnUpdate(float deltaTime) override {
        // No special update logic needed
    }

    void OnRender() override {
        // Debug UI is handled by VulkanRenderer
    }

    void OnShutdown() override {
        std::cout << "Point Light Test Mode Shutdown" << std::endl;
    }

private:
    void CreateTestScene() {
        if (!m_Scene) return;

        ModelLoader modelLoader;

        // Create ground plane material
        auto groundMaterial = std::make_shared<Material>();
        groundMaterial->diffuseColor = glm::vec3(0.5f, 0.5f, 0.5f);
        groundMaterial->setPBRProperties(0.0f, 0.6f); 
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
        groundTransform.position = glm::vec3(0.0f, -1.0f, 0.0f);
        groundTransform.scale = glm::vec3(100.0f, 1.0f, 100.0f);
        
        MeshData groundPlane = modelLoader.CreatePlane(1.0f, 1.0f);
        std::vector<MeshData> groundMeshes = { groundPlane };
        m_Scene->createMeshesFromData(groundMeshes, groundTransform, groundMaterial);

        // Create a central sphere to catch the light
        auto sphereMaterial = std::make_shared<Material>();
        sphereMaterial->diffuseColor = glm::vec3(1.0f, 1.0f, 1.0f); // White to show light colors
        sphereMaterial->setPBRProperties(0.1f, 0.3f);
        sphereMaterial->alpha = 1.0f;

        sphereMaterial->setTexture(TextureType::Diffuse, nullptr);
        sphereMaterial->setTexture(TextureType::Normal, nullptr);
        sphereMaterial->setTexture(TextureType::MetallicRoughness, nullptr);
        sphereMaterial->setTexture(TextureType::Emissive, nullptr);
        sphereMaterial->setTexture(TextureType::AmbientOcclusion, nullptr);

        VkDescriptorSet sphereDescriptorSet = m_Scene->getRenderer()->createMaterialDescriptorSet(*sphereMaterial);
        if (sphereDescriptorSet != VK_NULL_HANDLE) {
            sphereMaterial->setDescriptorSet(sphereDescriptorSet);
        }

        Transform sphereTransform;
        sphereTransform.position = glm::vec3(0.0f, 1.0f, 0.0f);
        sphereTransform.scale = glm::vec3(2.0f);

        MeshData sphereData = modelLoader.CreateSphere(1.0f, 32, 32);
        std::vector<MeshData> sphereMeshes = { sphereData };
        m_Scene->createMeshesFromData(sphereMeshes, sphereTransform, sphereMaterial);
        
        std::cout << "Created point light test scene" << std::endl;
    }
};
