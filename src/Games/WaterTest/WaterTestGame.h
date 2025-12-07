#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "loader/ModelLoader.h"
#include "Renderer/WaterSystem.h"
#include "VulkanRenderer.h"
#include <iostream>

class WaterTestGame : public Game {
public:
    void OnInit() override {
        std::cout << "Water Test Mode Initialized" << std::endl;

        // Get renderer reference
        VulkanRenderer* renderer = m_Scene ? m_Scene->getRenderer() : nullptr;
        if (!renderer) {
            std::cerr << "Failed to get renderer!" << std::endl;
            return;
        }

        // Initialize the water system
        renderer->initializeWater(256);  // 256x256 height field resolution

        // Configure water parameters
        if (WaterSystem* water = renderer->getWaterSystem()) {
            // Position water at y=0 (ground level)
            water->setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
            water->setScale(glm::vec3(100.0f, 1.0f, 100.0f));  // 100x100 unit water surface (larger pool)

            // Configure water appearance
            WaterParameters& params = water->getParameters();
            params.waveSpeed = 0.1f;   // Slow wave propagation (adjustable in debug panel)
            params.damping = 0.995f;   // Less damping so waves last longer
            params.heightScale = 0.5f; // More visible waves
            params.shallowColor = glm::vec3(0.1f, 0.4f, 0.5f);
            params.deepColor = glm::vec3(0.0f, 0.1f, 0.2f);
            params.fresnelPower = 5.0f;

            std::cout << "Water system configured" << std::endl;
        }

        // Setup Scene with lighting
        if (m_Scene) {
            m_Scene->clearLights();

            // Add a directional light (sun)
            m_Scene->addLight(
                glm::vec3(0.3f, -1.0f, 0.2f),     // Direction - angled sun
                glm::vec3(1.0f, 0.95f, 0.9f),     // Warm white color
                1.5f,                              // Intensity
                0.0f,                              // Radius (0 for directional)
                1.0f,                              // Falloff
                true                               // isDirectional = true
            );

            // Add an ambient fill light
            m_Scene->addLight(
                glm::vec3(0.0f, 1.0f, 0.0f),      // From below (fill)
                glm::vec3(0.6f, 0.7f, 0.9f),      // Cool blue ambient
                0.3f,                              // Low intensity
                0.0f,
                1.0f,
                true
            );
        }

        // Setup Camera - higher and further back to see the larger pool
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(50.0f, 30.0f, 50.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            m_Camera->setFarPlane(500.0f);
            m_Camera->setFOV(60.0f);
        }

        // Create the test scene geometry
        CreateTestScene();

        std::cout << "Water Test Scene Ready - Use mouse to look around, WASD to move" << std::endl;
        std::cout << "Press R to add a ripple at the center" << std::endl;
    }

    void OnUpdate(float deltaTime) override {
        VulkanRenderer* renderer = m_Scene ? m_Scene->getRenderer() : nullptr;
        if (!renderer) return;

        WaterSystem* water = renderer->getWaterSystem();
        if (!water) return;

        // Accumulate time for periodic ripples
        m_TimeSinceLastRipple += deltaTime;

        // Add periodic ripples for testing
        if (m_AutoRipple && m_TimeSinceLastRipple > m_RippleInterval) {
            // Random position within the water surface (away from edges)
            float x = 0.3f + static_cast<float>(rand()) / RAND_MAX * 0.4f;
            float y = 0.3f + static_cast<float>(rand()) / RAND_MAX * 0.4f;
            // Very small point-like ripple: radius 0.005 (0.5% of surface)
            water->addRipple(glm::vec2(x, y), 0.8f, 0.005f);
            m_TimeSinceLastRipple = 0.0f;
        }

        // Animate water color based on time (subtle day/night cycle simulation)
        m_TotalTime += deltaTime;
        float cycle = sin(m_TotalTime * 0.1f) * 0.5f + 0.5f;

        WaterParameters& params = water->getParameters();
        params.shallowColor = glm::mix(
            glm::vec3(0.1f, 0.4f, 0.5f),   // Day color
            glm::vec3(0.05f, 0.2f, 0.3f),  // Dusk color
            cycle * 0.3f
        );
    }

    void OnRender() override {
        // Debug UI is handled by VulkanRenderer
    }

    void OnShutdown() override {
        std::cout << "Water Test Mode Shutdown" << std::endl;
    }

    // Allow toggling auto-ripple
    void SetAutoRipple(bool enable) { m_AutoRipple = enable; }
    void SetRippleInterval(float interval) { m_RippleInterval = interval; }

private:
    float m_TimeSinceLastRipple = 0.0f;
    float m_RippleInterval = 2.0f;  // Add ripple every 2 seconds
    bool m_AutoRipple = false;  // Disabled - use debug panel to add ripples manually
    float m_TotalTime = 0.0f;

    void CreateTestScene() {
        if (!m_Scene) return;

        ModelLoader modelLoader;
        VulkanRenderer* renderer = m_Scene->getRenderer();

        // =========================================
        // Create underwater floor (visible through water)
        // =========================================
        auto floorMaterial = std::make_shared<Material>();
        floorMaterial->diffuseColor = glm::vec3(0.6f, 0.5f, 0.4f);  // Sandy color
        floorMaterial->setPBRProperties(0.0f, 0.8f);
        floorMaterial->alpha = 1.0f;

        floorMaterial->setTexture(TextureType::Diffuse, nullptr);
        floorMaterial->setTexture(TextureType::Normal, nullptr);
        floorMaterial->setTexture(TextureType::MetallicRoughness, nullptr);
        floorMaterial->setTexture(TextureType::Emissive, nullptr);
        floorMaterial->setTexture(TextureType::AmbientOcclusion, nullptr);

        VkDescriptorSet floorDescriptorSet = renderer->createMaterialDescriptorSet(*floorMaterial);
        if (floorDescriptorSet != VK_NULL_HANDLE) {
            floorMaterial->setDescriptorSet(floorDescriptorSet);
        }

        Transform floorTransform;
        floorTransform.position = glm::vec3(0.0f, -2.0f, 0.0f);  // Below water
        floorTransform.scale = glm::vec3(50.0f, 1.0f, 50.0f);

        MeshData floorPlane = modelLoader.CreatePlane(1.0f, 1.0f);
        std::vector<MeshData> floorMeshes = { floorPlane };
        m_Scene->createMeshesFromData(floorMeshes, floorTransform, floorMaterial);

        // =========================================
        // Create shore/bank around the water
        // =========================================
        auto shoreMaterial = std::make_shared<Material>();
        shoreMaterial->diffuseColor = glm::vec3(0.4f, 0.35f, 0.25f);  // Dirt/mud color
        shoreMaterial->setPBRProperties(0.0f, 0.9f);
        shoreMaterial->alpha = 1.0f;

        shoreMaterial->setTexture(TextureType::Diffuse, nullptr);
        shoreMaterial->setTexture(TextureType::Normal, nullptr);
        shoreMaterial->setTexture(TextureType::MetallicRoughness, nullptr);
        shoreMaterial->setTexture(TextureType::Emissive, nullptr);
        shoreMaterial->setTexture(TextureType::AmbientOcclusion, nullptr);

        VkDescriptorSet shoreDescriptorSet = renderer->createMaterialDescriptorSet(*shoreMaterial);
        if (shoreDescriptorSet != VK_NULL_HANDLE) {
            shoreMaterial->setDescriptorSet(shoreDescriptorSet);
        }

        // Create shore segments around the water
        struct ShoreSegment {
            glm::vec3 position;
            glm::vec3 scale;
        };

        std::vector<ShoreSegment> shores = {
            { glm::vec3(-20.0f, 0.5f, 0.0f), glm::vec3(10.0f, 1.0f, 40.0f) },  // Left bank
            { glm::vec3(20.0f, 0.5f, 0.0f), glm::vec3(10.0f, 1.0f, 40.0f) },   // Right bank
            { glm::vec3(0.0f, 0.5f, -20.0f), glm::vec3(40.0f, 1.0f, 10.0f) },  // Back bank
            { glm::vec3(0.0f, 0.5f, 20.0f), glm::vec3(40.0f, 1.0f, 10.0f) },   // Front bank
        };

        for (const auto& shore : shores) {
            Transform shoreTransform;
            shoreTransform.position = shore.position;
            shoreTransform.scale = shore.scale;

            MeshData shorePlane = modelLoader.CreatePlane(1.0f, 1.0f);
            std::vector<MeshData> shoreMeshes = { shorePlane };
            m_Scene->createMeshesFromData(shoreMeshes, shoreTransform, shoreMaterial);
        }

        // =========================================
        // Create some objects around the pond
        // =========================================
        struct SceneObject {
            glm::vec3 position;
            glm::vec3 color;
            float metallic;
            float roughness;
            float scale;
            bool isSphere;  // true = sphere, false = cube
        };

        std::vector<SceneObject> objects = {
            // Rocks around the pond
            { glm::vec3(-12.0f, 1.5f, 8.0f), glm::vec3(0.4f, 0.4f, 0.4f), 0.0f, 0.9f, 2.0f, true },
            { glm::vec3(10.0f, 1.0f, -10.0f), glm::vec3(0.5f, 0.45f, 0.4f), 0.0f, 0.85f, 1.5f, true },
            { glm::vec3(-8.0f, 0.8f, -12.0f), glm::vec3(0.35f, 0.35f, 0.35f), 0.0f, 0.95f, 1.2f, true },

            // Decorative spheres (like garden ornaments)
            { glm::vec3(12.0f, 2.0f, 5.0f), glm::vec3(0.8f, 0.2f, 0.2f), 0.0f, 0.3f, 1.0f, true },
            { glm::vec3(-10.0f, 2.0f, -8.0f), glm::vec3(0.2f, 0.6f, 0.8f), 0.5f, 0.2f, 1.0f, true },

            // Metallic sphere (mirror ball)
            { glm::vec3(8.0f, 2.5f, -6.0f), glm::vec3(0.9f, 0.9f, 0.9f), 1.0f, 0.05f, 1.2f, true },
        };

        for (const auto& obj : objects) {
            auto material = std::make_shared<Material>();
            material->diffuseColor = obj.color;
            material->setPBRProperties(obj.metallic, obj.roughness);
            material->alpha = 1.0f;

            material->setTexture(TextureType::Diffuse, nullptr);
            material->setTexture(TextureType::Normal, nullptr);
            material->setTexture(TextureType::MetallicRoughness, nullptr);
            material->setTexture(TextureType::Emissive, nullptr);
            material->setTexture(TextureType::AmbientOcclusion, nullptr);

            VkDescriptorSet materialDescriptorSet = renderer->createMaterialDescriptorSet(*material);
            if (materialDescriptorSet != VK_NULL_HANDLE) {
                material->setDescriptorSet(materialDescriptorSet);
            }

            Transform transform;
            transform.position = obj.position;
            transform.scale = glm::vec3(obj.scale);

            MeshData meshData = obj.isSphere
                ? modelLoader.CreateSphere(1.0f, 24, 24)
                : modelLoader.CreateCube(1.0f);
            std::vector<MeshData> meshes = { meshData };
            m_Scene->createMeshesFromData(meshes, transform, material);
        }

        std::cout << "Created water test scene with pond, shore, and " << objects.size() << " objects" << std::endl;
    }
};
