#pragma once
#include <memory>
#include <chrono>
#include "VulkanRenderer.h"
#include "Game.h"
#include "Input.h"
#include "project/ProjectManager.h"

class Application {
public:
    Application(std::unique_ptr<Game> game) : m_Game(std::move(game)) {
        m_Renderer = std::make_unique<VulkanRenderer>();

        // Initialize engine path for asset resolution
        ProjectManager::getInstance().setEnginePath(std::filesystem::current_path());
    }

    void Run() {
        // Initialize Renderer (Window, Vulkan, etc.)
        // We might need to split initWindow and initVulkan if we want to hook Input early
        m_Renderer->initWindow(); 
        Input::Initialize(m_Renderer->getWindow());
        m_Renderer->initVulkan();

        // Initialize Game
        m_Game->SetScene(m_Renderer->getScene());
        m_Game->SetCamera(m_Renderer->getCamera());
        m_Game->SetWorld(m_Renderer->getWorld());
        m_Game->SetRenderer(m_Renderer.get());
        m_Game->OnInit();

        // Disable renderer's internal camera update since we handle it here or in Game
        m_Renderer->setAutoUpdateCamera(false);

        // Main Loop
        auto lastTime = std::chrono::high_resolution_clock::now();

        while (!glfwWindowShouldClose(m_Renderer->getWindow())) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
            lastTime = currentTime;

            glfwPollEvents();

            // Update Game
            m_Game->OnUpdate(deltaTime);

            // Update Renderer (Camera, etc.)
            m_Renderer->updateCamera(deltaTime, m_Game->UsesDefaultCameraInput(), m_Game->UsesDefaultCameraMovement());
            // But for now, let's keep the existing camera controller working if possible, 
            // or better, let the Game call m_Camera->processKeyboard/Mouse.
            
            // Render
            m_Renderer->drawFrame();
        }

        m_Renderer->cleanup();
        m_Game->OnShutdown();
    }

private:
    std::unique_ptr<VulkanRenderer> m_Renderer;
    std::unique_ptr<Game> m_Game;
};
