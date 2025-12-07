#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "core/Input.h"
#include <iostream>

class SandboxGame : public Game {
public:
    void OnInit() override {
        std::cout << "SandboxGame Initialized" << std::endl;
        
        // Replicate the scene loading from VulkanRenderer
        if (m_Scene) {
            m_Scene->setupDefaultLighting();
            
            Transform modelTransform;
            modelTransform.position = glm::vec3(5.0f, 0.0f, 0.0f);
            modelTransform.scale = glm::vec3(19.0f);
            
            MaterialTexturePaths blackratTextures;
            blackratTextures.diffuse = "texture/blackrat_color.png";
            blackratTextures.normal = "texture/blackrat_normal.png";
            blackratTextures.metallic = "texture/blackrat_metal.png";
            blackratTextures.roughness = "texture/blackrat_rough.png";
            blackratTextures.specular = "texture/blackrat_spec.png";
            
            m_Scene->loadTexturedModelPBR("models/blackrat.fbx", blackratTextures, modelTransform);
        }
        
        // Set camera initial position
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(2.0f, 2.0f, 2.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
        }
    }

    void OnUpdate(float deltaTime) override {
        // Game logic here
        if (Input::IsKeyPressed(GLFW_KEY_ESCAPE)) {
            // Handle escape
        }
    }

    void OnRender() override {
        // Custom rendering if needed (e.g. UI)
    }

    void OnShutdown() override {
        std::cout << "SandboxGame Shutdown" << std::endl;
    }
};
