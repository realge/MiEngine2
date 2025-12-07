#include "debug/WaterDebugPanel.h"
#include "VulkanRenderer.h"
#include "Renderer/WaterSystem.h"
#include "imgui.h"

WaterDebugPanel::WaterDebugPanel(VulkanRenderer* renderer)
    : DebugPanel("Water Debug", renderer) {
}

void WaterDebugPanel::draw() {
    ImGui::SetNextWindowPos(ImVec2(10, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(panelName.c_str(), &isOpen)) {
        WaterSystem* water = renderer->getWaterSystem();
        if (!water || !water->isReady()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Water System Not Available");
            ImGui::End();
            return;
        }

        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Water System Active");
        ImGui::Separator();

        drawRippleControls();
        ImGui::Separator();
        drawWaterParameters();
        ImGui::Separator();
        drawEnvironmentControls();
        ImGui::Separator();
        drawSimulationInfo();
    }
    ImGui::End();
}

void WaterDebugPanel::drawRippleControls() {
    WaterSystem* water = renderer->getWaterSystem();
    if (!water) return;

    ImGui::Text("Ripple Controls");
    ImGui::Indent();

    ImGui::SliderFloat("Position X", &ripplePosX, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Position Y", &ripplePosY, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Strength", &rippleStrength, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Radius", &rippleRadius, 0.001f, 0.2f, "%.4f");

    // Visual indicator for radius
    ImGui::Text("Radius: %.1f%% of surface", rippleRadius * 100.0f);

    if (ImGui::Button("Add Ripple")) {
        water->addRipple(glm::vec2(ripplePosX, ripplePosY), rippleStrength, rippleRadius);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Center Ripple")) {
        water->addRipple(glm::vec2(0.5f, 0.5f), rippleStrength, rippleRadius);
    }

    // Quick presets
    ImGui::Text("Presets:");
    if (ImGui::Button("Tiny")) {
        rippleRadius = 0.005f;
        rippleStrength = 1.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Small")) {
        rippleRadius = 0.01f;
        rippleStrength = 0.8f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Medium")) {
        rippleRadius = 0.03f;
        rippleStrength = 0.5f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Large")) {
        rippleRadius = 0.08f;
        rippleStrength = 0.3f;
    }

    ImGui::Unindent();
}

void WaterDebugPanel::drawWaterParameters() {
    WaterSystem* water = renderer->getWaterSystem();
    if (!water) return;

    WaterParameters& params = water->getParameters();

    ImGui::Text("Simulation Parameters");
    ImGui::Indent();

    ImGui::SliderFloat("Wave Speed", &params.waveSpeed, 0.01f, 0.5f, "%.3f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Controls how fast waves propagate. Keep < 0.5 for stability.");
    }
    ImGui::SliderFloat("Damping", &params.damping, 0.9f, 1.0f, "%.4f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How quickly waves lose energy. 1.0 = no damping, 0.9 = fast decay");
    }
    ImGui::SliderFloat("Height Scale", &params.heightScale, 0.01f, 2.0f, "%.3f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Visual height multiplier for rendering");
    }

    ImGui::Unindent();

    ImGui::Text("Visual Parameters");
    ImGui::Indent();

    ImGui::ColorEdit3("Shallow Color", &params.shallowColor.x);
    ImGui::ColorEdit3("Deep Color", &params.deepColor.x);
    ImGui::SliderFloat("Fresnel Power", &params.fresnelPower, 1.0f, 10.0f, "%.1f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Higher values = more reflection at grazing angles");
    }
    ImGui::SliderFloat("Reflection Strength", &params.reflectionStrength, 0.0f, 2.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("IBL reflection intensity. 0 = no reflection, 1 = normal, 2 = bright");
    }
    ImGui::SliderFloat("Specular Power", &params.specularPower, 8.0f, 512.0f, "%.0f");

    ImGui::Unindent();

    ImGui::Text("Transform");
    ImGui::Indent();

    glm::vec3 pos = water->getPosition();
    glm::vec3 scale = water->getScale();

    if (ImGui::DragFloat3("Position", &pos.x, 0.5f)) {
        water->setPosition(pos);
    }
    if (ImGui::DragFloat3("Scale", &scale.x, 1.0f, 1.0f, 500.0f)) {
        water->setScale(scale);
    }

    ImGui::Unindent();
}

void WaterDebugPanel::drawSimulationInfo() {
    WaterSystem* water = renderer->getWaterSystem();
    if (!water) return;

    ImGui::Text("Simulation Info");
    ImGui::Indent();

    glm::vec3 scale = water->getScale();
    ImGui::Text("Pool Size: %.0f x %.0f units", scale.x, scale.z);
    ImGui::Text("Height Map: 256 x 256");

    // Show what the current ripple radius means in world units
    ImGui::Text("Current Ripple Settings:");
    ImGui::Text("  Radius: %.4f UV = %.2f world units", rippleRadius, rippleRadius * scale.x);
    ImGui::Text("  On 256x256 grid: %.1f pixels diameter", rippleRadius * 256.0f * 2.0f);

    ImGui::Separator();
    ImGui::Text("Expected Behavior:");
    ImGui::TextWrapped("A ripple should create a small circular disturbance that expands OUTWARD as a ring.");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Debug: Try these steps:");
    ImGui::TextWrapped("1. Set Radius to 0.01 (Tiny preset)");
    ImGui::TextWrapped("2. Set Strength to 1.0");
    ImGui::TextWrapped("3. Click 'Add Center Ripple'");
    ImGui::TextWrapped("4. Watch if wave expands OUT or IN");

    ImGui::Unindent();
}

void WaterDebugPanel::drawEnvironmentControls() {
    ImGui::Text("Environment / Skybox");
    ImGui::Indent();

    // Available HDR files
    const char* hdrOptions[] = { "sky.hdr", "test.hdr" };
    const char* hdrPaths[] = { "hdr/sky.hdr", "hdr/test.hdr" };
    const int numOptions = 2;

    ImGui::Text("Current HDR:");
    if (ImGui::Combo("##HDRSelect", &currentHDRIndex, hdrOptions, numOptions)) {
        // User selected a new HDR
        if (renderer->setupIBL(hdrPaths[currentHDRIndex])) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "HDR loaded!");
        }
    }

    if (ImGui::Button("Reload Current HDR")) {
        renderer->setupIBL(hdrPaths[currentHDRIndex]);
    }

    // Show IBL status
    if (renderer->isIBLReady()) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "IBL: Ready");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "IBL: Not Ready");
    }

    ImGui::Unindent();
}
