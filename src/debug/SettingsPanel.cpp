#include "debug/SettingsPanel.h"
#include "VulkanRenderer.h"
#include <imgui.h>


SettingsPanel::SettingsPanel(VulkanRenderer* renderer)
    : DebugPanel("Settings", renderer) {
}

void SettingsPanel::draw() {
    ImGui::SetNextWindowPos(ImVec2(780, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 250), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin(panelName.c_str(), &isOpen)) {
        if (ImGui::CollapsingHeader("Camera Settings")) {
            drawCameraSettings();
        }
        
        if (ImGui::CollapsingHeader("Render Settings")) {
            drawRenderSettings();
        }
        
        if (ImGui::CollapsingHeader("Debug Settings")) {
            drawDebugSettings();
        }
    }
    ImGui::End();
}

void SettingsPanel::drawCameraSettings() {
    if (!renderer->getCamera()) return;
    
    Camera* camera = renderer->getCamera();
    
    float fov = camera->getFOV();
    if (ImGui::SliderFloat("Field of View", &fov, 30.0f, 120.0f)) {
        camera->setFOV(fov);
    }
    
    float speed = camera->getMovementSpeed();
    if (ImGui::SliderFloat("Movement Speed", &speed, 1.0f, 50.0f)) {
        camera->setMovementSpeed(speed);
    }
    
    float sensitivity = camera->getMouseSensitivity();
    if (ImGui::SliderFloat("Mouse Sensitivity", &sensitivity, 0.01f, 1.0f)) {
        camera->setMouseSensitivity(sensitivity);
    }
}

void SettingsPanel::drawRenderSettings() {
    bool vsync = false; // You'd need to track this in VulkanRenderer
    ImGui::Checkbox("VSync", &vsync);
    
    // Add more render settings as needed
}

void SettingsPanel::drawDebugSettings() {
    if (ImGui::Button("Reset Camera")) {
        Camera* camera = renderer->getCamera();
        if (camera) {
            camera->setPosition(glm::vec3(2.0f, 2.0f, 2.0f));
            camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
        }
    }
    
    if (ImGui::Button("Reload Shaders")) {
        // Implement shader reloading
    }
}