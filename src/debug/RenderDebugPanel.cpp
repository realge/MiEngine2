#include "debug/RenderDebugPanel.h"
#include "VulkanRenderer.h"
#include "Renderer/ShadowSystem.h"
#include "Renderer/PointLightShadowSystem.h"

RenderDebugPanel::RenderDebugPanel(VulkanRenderer* renderer)
    : DebugPanel("Render Debug", renderer) {
}

void RenderDebugPanel::draw() {
    ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin(panelName.c_str(), &isOpen)) {
        drawRenderModeSection();
        ImGui::Separator();
        drawSceneStatistics();
        ImGui::Separator();
        drawViewportInfo();
        ImGui::Separator();
        drawPipelineInfo();
        ImGui::Separator();
        drawLightDebug();
    }
    ImGui::End();
}

void RenderDebugPanel::drawRenderModeSection() {
    const char* renderModeStr = "Unknown";
    RenderMode mode = renderer->getRenderMode();

    switch(mode) {
        case RenderMode::Standard: renderModeStr = "Standard"; break;
        case RenderMode::PBR: renderModeStr = "PBR"; break;
        case RenderMode::PBR_IBL: renderModeStr = "PBR with IBL"; break;
    }

    ImGui::Text("Render Mode: %s", renderModeStr);

    // Render mode selector
    // Render mode selector
    if (ImGui::BeginCombo("Change Mode", renderModeStr)) {
        if (ImGui::Selectable("Standard", mode == RenderMode::Standard)) {
            renderer->setRenderMode(RenderMode::Standard);
        }
        if (ImGui::Selectable("PBR", mode == RenderMode::PBR)) {
            renderer->setRenderMode(RenderMode::PBR);
        }
        // Use getIBLSystem()->isReady() for safety
        bool iblReady = renderer->getIBLSystem() && renderer->getIBLSystem()->isReady();
        if (iblReady && ImGui::Selectable("PBR with IBL", mode == RenderMode::PBR_IBL)) {
            renderer->setRenderMode(RenderMode::PBR_IBL);
        }
        ImGui::EndCombo();
    }

    // Scene Controls
    if (ImGui::Button("Load Sphere Grid Test")) {
        renderer->createPBRIBLTestScene();
    }

    // IBL Toggle Checkbox
    bool iblReady = renderer->getIBLSystem() && renderer->getIBLSystem()->isReady();
    if (iblReady) {
        bool useIBL = (mode == RenderMode::PBR_IBL);
        if (ImGui::Checkbox("Enable IBL", &useIBL)) {
            renderer->setRenderMode(useIBL ? RenderMode::PBR_IBL : RenderMode::PBR);
        }
        
        if (useIBL) {
            float intensity = renderer->getIBLIntensity();
            if (ImGui::SliderFloat("IBL Intensity", &intensity, 0.0f, 5.0f)) {
                renderer->setIBLIntensity(intensity);
            }
        }
    } else {
        ImGui::TextDisabled("IBL Not Available");
    }

    // Debug layer selector
    ImGui::Spacing();
    const char* debugLayerNames[] = {
        "0: Full Rendering",
        "1: Direct Lighting Only",
        "2: Diffuse IBL Only",
        "3: Specular IBL Only",
        "4: BRDF LUT (Scale)",
        "5: BRDF LUT (Bias)",
        "6: Prefiltered Env Map",
        "7: Full Ambient (IBL)",
        "8: Irradiance Map (Raw)",
        "9: NdotV Visualization",
        "10: Roughness Visualization",
        "11: BRDF LUT Coords (RG)",
        "12: Shadow (White=Lit)",
        "13: Shadow Map Depth",
        "14: Light Space Z",
        "15: RT Shadow",
        "16: Vertex Colors (Clusters)",
        "17: Albedo Only"
    };
    const int numDebugLayers = sizeof(debugLayerNames) / sizeof(debugLayerNames[0]);

    int currentLayer = renderer->getDebugLayer();
    // Clamp to valid range for safety
    if (currentLayer < 0 || currentLayer >= numDebugLayers) currentLayer = 0;

    if (ImGui::BeginCombo("Debug Layer", debugLayerNames[currentLayer])) {
        for (int i = 0; i < numDebugLayers; i++) {
            if (ImGui::Selectable(debugLayerNames[i], currentLayer == i)) {
                renderer->setDebugLayer(i);
            }
        }
        ImGui::EndCombo();
    }

    // Shadow debugging info
    if (currentLayer >= 12 && currentLayer <= 14) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Shadow Debug Mode Active");
        if (currentLayer == 12) {
            ImGui::TextWrapped("White = Fully lit, Black = In shadow");
        } else if (currentLayer == 13) {
            ImGui::TextWrapped("Shows depth values stored in shadow map");
        } else if (currentLayer == 14) {
            ImGui::TextWrapped("Shows fragment Z in light space");
        }
    }

    // Shadow system toggles
    ImGui::Spacing();
    ImGui::Text("Shadow Settings:");

    ShadowSystem* shadowSystem = renderer->getShadowSystem();
    PointLightShadowSystem* pointShadowSystem = renderer->getPointLightShadowSystem();

    if (shadowSystem) {
        bool dirShadowEnabled = shadowSystem->isEnabled();
        if (ImGui::Checkbox("Directional Shadows", &dirShadowEnabled)) {
            shadowSystem->setEnabled(dirShadowEnabled);
        }

        if (dirShadowEnabled) {
            ImGui::Indent();

            // Shadow map resolution display
            ImGui::Text("Resolution: %dx%d", shadowSystem->getShadowMapWidth(), shadowSystem->getShadowMapHeight());

            // Frustum size slider
            float frustumSize = shadowSystem->getFrustumSize();
            if (ImGui::SliderFloat("Frustum Size", &frustumSize, 1.0f, 200.0f, "%.1f")) {
                shadowSystem->setFrustumSize(frustumSize);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Smaller = sharper shadows but covers less area\nLarger = softer shadows but covers more area");
            }

            // Depth bias controls
            float biasConstant = shadowSystem->getDepthBiasConstant();
            float biasSlope = shadowSystem->getDepthBiasSlopeFactor();
            bool biasChanged = false;

            if (ImGui::SliderFloat("Bias Constant", &biasConstant, 0.0f, 10.0f, "%.2f")) {
                biasChanged = true;
            }
            if (ImGui::SliderFloat("Bias Slope", &biasSlope, 0.0f, 10.0f, "%.2f")) {
                biasChanged = true;
            }
            if (biasChanged) {
                shadowSystem->setBias(biasConstant, biasSlope);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Adjust to reduce shadow acne or peter-panning");
            }

            // Depth range
            float nearPlane = shadowSystem->getNearPlane();
            float farPlane = shadowSystem->getFarPlane();
            bool rangeChanged = false;

            if (ImGui::SliderFloat("Near Plane", &nearPlane, 0.01f, 10.0f, "%.2f")) {
                rangeChanged = true;
            }
            if (ImGui::SliderFloat("Far Plane", &farPlane, 10.0f, 500.0f, "%.1f")) {
                rangeChanged = true;
            }
            if (rangeChanged) {
                shadowSystem->setDepthRange(nearPlane, farPlane);
            }

            ImGui::Unindent();
        }
    }

    if (pointShadowSystem) {
        bool pointShadowEnabled = pointShadowSystem->isEnabled();
        if (ImGui::Checkbox("Point Light Shadows", &pointShadowEnabled)) {
            pointShadowSystem->setEnabled(pointShadowEnabled);
        }
        if (pointShadowEnabled) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(6 passes per light!)");
        }
    }
}

void RenderDebugPanel::drawSceneStatistics() {
    Scene* scene = renderer->getScene();
    if (scene) {
        ImGui::Text("Scene Statistics:");
        ImGui::Indent();
        ImGui::Text("Lights: %zu", scene->getLights().size());
        ImGui::Text("Mesh Instances: %zu", scene->getMeshInstances().size());
        ImGui::Unindent();
    }
}

void RenderDebugPanel::drawViewportInfo() {
    VkExtent2D extent = renderer->getSwapChainExtent();
    ImGui::Text("Viewport:");
    ImGui::Indent();
    ImGui::Text("Resolution: %dx%d", extent.width, extent.height);
    ImGui::Text("Aspect Ratio: %.2f", extent.width / (float)extent.height);
    ImGui::Text("Near Plane: %.3f", renderer->getNearPlane());
    ImGui::Text("Far Plane: %.1f", renderer->getFarPlane());
    ImGui::Unindent();
}

void RenderDebugPanel::drawPipelineInfo() {
    ImGui::Text("Pipeline Status:");
    ImGui::Indent();
    
    bool pbrEnabled = renderer->isPBRPipelineReady();
    bool iblEnabled = renderer->isIBLReady();
    bool skyboxEnabled = renderer->isSkyboxReady();
    
    ImGui::TextColored(pbrEnabled ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
                      "PBR: %s", pbrEnabled ? "Ready" : "Not Available");
    
    ImGui::TextColored(iblEnabled ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
                      "IBL: %s", iblEnabled ? "Ready" : "Not Available");
    
    ImGui::TextColored(skyboxEnabled ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
                      "Skybox: %s", skyboxEnabled ? "Ready" : "Not Available");
    
    ImGui::Unindent();
}

void RenderDebugPanel::drawLightDebug() {
    Scene* scene = renderer->getScene();
    if (!scene) return;

    if (ImGui::CollapsingHeader("Light Debugging")) {
        // Get non-const lights
        std::vector<Scene::Light>& lights = scene->getLights();
        bool lightsChanged = false;

        for (size_t i = 0; i < lights.size(); i++) {
            ImGui::PushID(static_cast<int>(i));
            
            char label[32];
            snprintf(label, sizeof(label), "Light %zu (%s)", i, lights[i].isDirectional ? "Directional" : "Point");
            
            if (ImGui::TreeNode(label)) {
                Scene::Light& light = lights[i];
                
                ImGui::Text("Transform");
                // Position/Direction
                if (light.isDirectional) {
                    if (ImGui::DragFloat3("Direction", &light.position.x, 0.01f, -1.0f, 1.0f)) lightsChanged = true;
                } else {
                    if (ImGui::DragFloat3("Position (X,Y,Z)", &light.position.x, 0.1f)) lightsChanged = true;
                }
                
                ImGui::Separator();
                ImGui::Text("Properties");
                
                // Color
                if (ImGui::ColorEdit3("Color", &light.color.x)) lightsChanged = true;
                
                // Intensity
                if (ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f)) lightsChanged = true;
                
                // Radius
                if (ImGui::DragFloat("Radius", &light.radius, 0.5f, 0.1f, 1000.0f)) lightsChanged = true;
                
                // Falloff
                if (ImGui::DragFloat("Falloff", &light.falloff, 0.01f, 0.0f, 10.0f)) lightsChanged = true;
                
                ImGui::TreePop();
            }
            
            ImGui::PopID();
        }
        
        if (lightsChanged) {
            renderer->updateLights();
        }
    }
}
