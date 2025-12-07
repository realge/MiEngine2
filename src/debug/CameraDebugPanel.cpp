#include "debug/CameraDebugPanel.h"
#include "VulkanRenderer.h"
#include <imgui.h>
#include <glm/vec3.hpp>


CameraDebugPanel::CameraDebugPanel(VulkanRenderer* renderer)
    : DebugPanel("Camera Debug", renderer) {
}

void CameraDebugPanel::draw() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin(panelName.c_str(), &isOpen)) {
        if (renderer->getCamera()) {
            drawPositionSection();
            ImGui::Separator();
            drawRotationSection();
            
            if (showVectors) {
                ImGui::Separator();
                drawVectorSection();
            }
            
            if (showSettings) {
                ImGui::Separator();
                drawSettingsSection();
            }
            
            ImGui::Separator();
            drawControlsSection();
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Camera not initialized!");
        }
    }
    ImGui::End();
}

void CameraDebugPanel::drawPositionSection() {
    Camera* camera = renderer->getCamera();
    glm::vec3 pos = camera->getPosition();
    
    ImGui::Text("Position:");
    ImGui::Indent();
    ImGui::Text("X: %.2f", pos.x);
    ImGui::SameLine(120); ImGui::Text("Y: %.2f", pos.y);
    ImGui::SameLine(220); ImGui::Text("Z: %.2f", pos.z);
    ImGui::Unindent();
}

void CameraDebugPanel::drawRotationSection() {
    Camera* camera = renderer->getCamera();
    
    ImGui::Text("Rotation:");
    ImGui::Indent();
    
    float yaw = camera->getYaw();
    float pitch = camera->getPitch();
    
    ImGui::Text("Yaw:   %7.2f°", yaw);
    ImGui::SameLine(180);
    ImGui::ProgressBar((yaw + 180.0f) / 360.0f, ImVec2(100, 0));
    
    ImGui::Text("Pitch: %7.2f°", pitch);
    ImGui::SameLine(180);
    ImGui::ProgressBar((pitch + 90.0f) / 180.0f, ImVec2(100, 0));
    
    ImGui::Unindent();
}

void CameraDebugPanel::drawVectorSection() {
    Camera* camera = renderer->getCamera();
    glm::vec3 front = camera->getFront();
    glm::vec3 right = camera->getRight();
    glm::vec3 up = camera->getUp();
    
    ImGui::Text("Direction Vectors:");
    ImGui::Indent();
    ImGui::Text("Front: (%.2f, %.2f, %.2f)", front.x, front.y, front.z);
    ImGui::Text("Right: (%.2f, %.2f, %.2f)", right.x, right.y, right.z);
    ImGui::Text("Up:    (%.2f, %.2f, %.2f)", up.x, up.y, up.z);
    ImGui::Unindent();
}

void CameraDebugPanel::drawSettingsSection() {
    Camera* camera = renderer->getCamera();
    
    ImGui::Text("Settings:");
    ImGui::Indent();
    
    float fov = camera->getFOV();
    if (ImGui::SliderFloat("FOV", &fov, 30.0f, 120.0f)) {
        camera->setFOV(fov);
    }
    
    float speed = camera->getMovementSpeed();
    if (ImGui::SliderFloat("Speed", &speed, 1.0f, 50.0f)) {
        camera->setMovementSpeed(speed);
    }
    
    float sensitivity = camera->getMouseSensitivity();
    if (ImGui::SliderFloat("Sensitivity", &sensitivity, 0.01f, 1.0f)) {
        camera->setMouseSensitivity(sensitivity);
    }
    
    ImGui::Unindent();
}

void CameraDebugPanel::drawControlsSection() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Controls:");
    ImGui::Indent();
    ImGui::Text("WASD - Move");
    ImGui::Text("Space/Shift - Up/Down");
    ImGui::Text("Mouse - Look around");
    ImGui::Text("Scroll - Zoom");
    ImGui::Text("ESC - Release mouse");
    ImGui::Unindent();
}