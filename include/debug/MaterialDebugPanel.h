#pragma once
#include "DebugPanel.h"
#include <unordered_map>

class MaterialDebugPanel : public DebugPanel {
public:
    MaterialDebugPanel(VulkanRenderer* renderer);
    void draw() override;
    
private:
    void drawMaterialProperties();
    void drawTextureInfo();
    void drawGlobalMaterialControls();
    
    // Global material override values
    float globalRoughness = 0.5f;
    float globalMetallic = 0.0f;
    bool overrideRoughness = false;
    bool overrideMetallic = false;
    
    // Per-mesh material overrides (mesh index -> override value)
    std::unordered_map<size_t, float> meshRoughnessOverrides;
    std::unordered_map<size_t, float> meshMetallicOverrides;
    
    int selectedMeshIndex = 0;
};