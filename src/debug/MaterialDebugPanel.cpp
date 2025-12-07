#include "debug/MaterialDebugPanel.h"
#include "VulkanRenderer.h"
#include "scene/Scene.h"

MaterialDebugPanel::MaterialDebugPanel(VulkanRenderer* renderer)
    : DebugPanel("Material Debug", renderer) {
}

void MaterialDebugPanel::draw() {
    ImGui::SetNextWindowPos(ImVec2(10, 680), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 300), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin(panelName.c_str(), &isOpen)) {
        drawGlobalMaterialControls();
        ImGui::Separator();
        drawMaterialProperties();
        ImGui::Separator();
        drawTextureInfo();
    }
    ImGui::End();
}

void MaterialDebugPanel::drawGlobalMaterialControls() {
    ImGui::Text("Global Material Overrides");
    ImGui::Indent();
    
    // Roughness slider
    ImGui::Checkbox("Override Roughness", &overrideRoughness);
    if (overrideRoughness) {
        if (ImGui::SliderFloat("Global Roughness", &globalRoughness, 0.0f, 1.0f, "%.3f")) {
            // Apply to all mesh instances
            Scene* scene = renderer->getScene();
            if (scene) {
                const auto& meshInstances = scene->getMeshInstances();
                for (size_t i = 0; i < meshInstances.size(); ++i) {
                    if (meshInstances[i].mesh) {
                        const auto& material = meshInstances[i].mesh->getMaterial();
                        if (material) {
                            material->roughness = globalRoughness;
                        }
                    }
                }
            }
        }
    }
    
    // Metallic slider
    ImGui::Checkbox("Override Metallic", &overrideMetallic);
    if (overrideMetallic) {
        if (ImGui::SliderFloat("Global Metallic", &globalMetallic, 0.0f, 1.0f, "%.3f")) {
            // Apply to all mesh instances
            Scene* scene = renderer->getScene();
            if (scene) {
                const auto& meshInstances = scene->getMeshInstances();
                for (size_t i = 0; i < meshInstances.size(); ++i) {
                    if (meshInstances[i].mesh) {
                        const auto& material = meshInstances[i].mesh->getMaterial();
                        if (material) {
                            material->metallic = globalMetallic;
                        }
                    }
                }
            }
        }
    }
    
    ImGui::Unindent();
}

void MaterialDebugPanel::drawMaterialProperties() {
    Scene* scene = renderer->getScene();
    if (!scene) {
        ImGui::Text("No scene loaded");
        return;
    }
    
    const auto& meshInstances = scene->getMeshInstances();
    if (meshInstances.empty()) {
        ImGui::Text("No mesh instances in scene");
        return;
    }
    
    ImGui::Text("Per-Mesh Material Properties");
    ImGui::Indent();
    
    // Mesh selector
    std::vector<std::string> meshNameStrings;
    std::vector<const char*> meshNames;
    meshNameStrings.reserve(meshInstances.size());
    meshNames.reserve(meshInstances.size());

    for (size_t i = 0; i < meshInstances.size(); ++i) {
        meshNameStrings.push_back("Mesh " + std::to_string(i));
        meshNames.push_back(meshNameStrings.back().c_str());
    }
    
    if (ImGui::BeginCombo("Select Mesh", selectedMeshIndex < meshNames.size() ? meshNames[selectedMeshIndex] : "None")) {
        for (size_t i = 0; i < meshInstances.size(); ++i) {
            bool isSelected = (selectedMeshIndex == i);
            if (ImGui::Selectable(meshNames[i], isSelected)) {
                selectedMeshIndex = static_cast<int>(i);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    // Show properties for selected mesh
    if (selectedMeshIndex < meshInstances.size()) {
        const auto& meshInstance = meshInstances[selectedMeshIndex];
        if (meshInstance.mesh) {
            const auto& materialPtr = meshInstance.mesh->getMaterial();
            if (materialPtr) {
                Material* material = materialPtr.get();
                
                ImGui::Text("Current Properties:");
                ImGui::Indent();
                
                // Individual roughness control for selected mesh
                float currentRoughness = material->roughness;
                if (ImGui::SliderFloat("Roughness", &currentRoughness, 0.0f, 1.0f, "%.3f")) {
                    material->roughness = currentRoughness;
                    meshRoughnessOverrides[selectedMeshIndex] = currentRoughness;
                }
                
                // Individual metallic control for selected mesh
                float currentMetallic = material->metallic;
                if (ImGui::SliderFloat("Metallic", &currentMetallic, 0.0f, 1.0f, "%.3f")) {
                    material->metallic = currentMetallic;
                    meshMetallicOverrides[selectedMeshIndex] = currentMetallic;
                }
                
                // Color controls
                ImGui::ColorEdit3("Diffuse Color", &material->diffuseColor[0]);
                ImGui::ColorEdit3("Emissive Color", &material->emissiveColor[0]);
                
                // Other properties
                ImGui::SliderFloat("Alpha", &material->alpha, 0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("Emissive Strength", &material->emissiveStrength, 0.0f, 10.0f, "%.2f");
                
                ImGui::Unindent();
            }
        }
    }
    
    ImGui::Unindent();
}

void MaterialDebugPanel::drawTextureInfo() {
    Scene* scene = renderer->getScene();
    if (!scene) return;
    
    const auto& meshInstances = scene->getMeshInstances();
    if (selectedMeshIndex >= meshInstances.size()) return;
    
    const auto& meshInstance = meshInstances[selectedMeshIndex];
    if (!meshInstance.mesh) return;
    
    const auto& materialPtr = meshInstance.mesh->getMaterial();
    if (!materialPtr) return;
    
    Material* material = materialPtr.get();
    
    ImGui::Text("Texture Information");
    ImGui::Indent();
    
    // Check for each texture type
    const char* textureTypes[] = {
        "Diffuse", "Normal", "Metallic", "Roughness", 
        "MetallicRoughness", "AmbientOcclusion", "Emissive"
    };
    
    for (int i = 0; i < 7; ++i) {
        bool hasTexture = material->hasTexture(static_cast<TextureType>(i));
        ImGui::TextColored(
            hasTexture ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "%s: %s", textureTypes[i], hasTexture ? "Loaded" : "Not Available"
        );
    }
    
    ImGui::Unindent();
}