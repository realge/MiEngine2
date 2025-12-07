#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "loader/ModelLoader.h"
#include <iostream>

class EditorGame : public Game {
public:
    void OnInit() override {
        std::cout << "Editor Mode Initialized - RAT VERSION" << std::endl;

        // Setup Scene
        if (m_Scene) {
            m_Scene->setupDefaultLighting();
            
            // Add a directional light
            m_Scene->addLight(
                glm::vec3(2.0f, 2.0f, 2.0f),
                glm::vec3(1.0f, 1.0f, 1.0f),
                1.0f, 0.0f, 1.0f, true
            );
        }

        // Setup Camera for Editor (Free Fly)
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            m_Camera->setFarPlane(100.0f);
            m_Camera->setFOV(45.0f);
        }

        // Load Robot Model (No Textures)
        if (m_Scene) {
            MaterialTexturePaths texturePaths;
            // No textures for Robot as requested
            
            // Adjust transform as needed
            m_Scene->loadPBRModel(
                "models/test_model.fbx",
                texturePaths,
                glm::vec3(0.0f, 0.0f, 0.0f), // Position (Centered)
                glm::vec3(0.0f, 0.0f, 0.0f), // Rotation (Reset)
                glm::vec3(1.0f)               // Scale (Standard)
            );
        }
    }

    void OnUpdate(float deltaTime) override {
        // No game logic in Editor Mode
        // Camera is handled by VulkanRenderer::updateCamera
    }

    void OnRender() override {
        // Debug UI is handled by VulkanRenderer
    }

    void OnShutdown() override {
        std::cout << "Editor Mode Shutdown" << std::endl;
    }
};
