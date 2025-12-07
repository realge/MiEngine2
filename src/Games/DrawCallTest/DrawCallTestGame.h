#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "loader/ModelLoader.h"
#include <iostream>
#include <random>

class DrawCallTestGame : public Game {
public:
    void OnInit() override {
        std::cout << "Draw Call Stress Test Initialized" << std::endl;
        std::cout << "Target: 10,000+ draw calls per frame" << std::endl;

        // Setup Scene - minimal lighting (no shadows for pure draw call test)
        if (m_Scene) {
            m_Scene->clearLights();

            // Single directional light, no point lights (to avoid shadow overhead)
            m_Scene->addLight(
                glm::vec3(1.0f, -1.0f, 0.5f),
                glm::vec3(1.0f, 1.0f, 1.0f),
                1.5f,
                0.0f,
                1.0f,
                true  // directional
            );
        }

        // Disable shadows for pure draw call testing
        if (m_Scene && m_Scene->getRenderer()) {
            auto* shadowSystem = m_Scene->getRenderer()->getShadowSystem();
            auto* pointShadowSystem = m_Scene->getRenderer()->getPointLightShadowSystem();
            if (shadowSystem) shadowSystem->setEnabled(false);
            if (pointShadowSystem) pointShadowSystem->setEnabled(false);
            std::cout << "Shadows disabled for draw call stress test" << std::endl;
        }

        // Setup Camera
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(0.0f, 50.0f, 100.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            m_Camera->setFarPlane(500.0f);
            m_Camera->setFOV(60.0f);
        }

        // Create massive grid of objects
        CreateStressTestScene();
    }

    void OnUpdate(float deltaTime) override {
        // Could add rotation animation here if desired
    }

    void OnRender() override {
        // Debug UI handled by VulkanRenderer
    }

    void OnShutdown() override {
        std::cout << "Draw Call Stress Test Shutdown" << std::endl;
    }

private:
    void CreateStressTestScene() {
        if (!m_Scene) return;

        ModelLoader modelLoader;
        std::mt19937 rng(42); // Fixed seed for reproducibility
        std::uniform_real_distribution<float> colorDist(0.2f, 1.0f);
        std::uniform_real_distribution<float> metalDist(0.0f, 1.0f);
        std::uniform_real_distribution<float> roughDist(0.1f, 0.9f);

        // Grid configuration - 100x100 = 10,000 objects
        const int gridSize = 100;
        const float spacing = 2.5f;
        const float startOffset = -(gridSize * spacing) / 2.0f;

        std::cout << "Creating " << (gridSize * gridSize) << " mesh instances..." << std::endl;

        // Create a few shared materials to reduce descriptor set allocations
        // but still have variety (10 different materials)
        const int numMaterials = 10;
        std::vector<std::shared_ptr<Material>> materials;

        for (int i = 0; i < numMaterials; i++) {
            auto material = std::make_shared<Material>();
            material->diffuseColor = glm::vec3(
                colorDist(rng),
                colorDist(rng),
                colorDist(rng)
            );
            material->setPBRProperties(metalDist(rng), roughDist(rng));
            material->alpha = 1.0f;

            material->setTexture(TextureType::Diffuse, nullptr);
            material->setTexture(TextureType::Normal, nullptr);
            material->setTexture(TextureType::MetallicRoughness, nullptr);
            material->setTexture(TextureType::Emissive, nullptr);
            material->setTexture(TextureType::AmbientOcclusion, nullptr);

            VkDescriptorSet descSet = m_Scene->getRenderer()->createMaterialDescriptorSet(*material);
            if (descSet != VK_NULL_HANDLE) {
                material->setDescriptorSet(descSet);
            }
            materials.push_back(material);
        }

        // Create simple low-poly sphere for each instance (to keep triangle count reasonable)
        // 8 segments = ~128 triangles per sphere
        MeshData sphereData = modelLoader.CreateSphere(0.8f, 8, 8);

        int count = 0;
        for (int x = 0; x < gridSize; x++) {
            for (int z = 0; z < gridSize; z++) {
                Transform transform;
                transform.position = glm::vec3(
                    startOffset + x * spacing,
                    0.0f,
                    startOffset + z * spacing
                );
                transform.scale = glm::vec3(1.0f);

                // Cycle through materials
                auto& material = materials[(x + z) % numMaterials];

                // Create mesh with material
                auto mesh = std::make_shared<Mesh>(
                    m_Scene->getRenderer()->getDevice(),
                    m_Scene->getRenderer()->getPhysicalDevice(),
                    sphereData,
                    material
                );
                mesh->createBuffers(
                    m_Scene->getRenderer()->getCommandPool(),
                    m_Scene->getRenderer()->getGraphicsQueue()
                );

                m_Scene->addMeshInstance(mesh, transform);
                count++;

                // Progress indicator
                if (count % 1000 == 0) {
                    std::cout << "  Created " << count << " objects..." << std::endl;
                }
            }
        }

        // Calculate stats
        int trianglesPerSphere = static_cast<int>(sphereData.indices.size() / 3);
        int totalTriangles = count * trianglesPerSphere;

        std::cout << "\n=== Draw Call Stress Test Stats ===" << std::endl;
        std::cout << "Total mesh instances: " << count << std::endl;
        std::cout << "Draw calls per frame: " << count << " (+ skybox)" << std::endl;
        std::cout << "Triangles per object: " << trianglesPerSphere << std::endl;
        std::cout << "Total triangles: " << totalTriangles << " (~" << (totalTriangles / 1000000.0f) << "M)" << std::endl;
        std::cout << "Unique materials: " << numMaterials << std::endl;
        std::cout << "===================================" << std::endl;
    }
};
