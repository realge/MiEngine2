#pragma once
#include "DebugPanel.h"

namespace MiEngine {
    class RayTracingSystem;
}

class RayTracingDebugPanel : public DebugPanel {
public:
    RayTracingDebugPanel(VulkanRenderer* renderer);
    void draw() override;

private:
    void drawStatusSection();
    void drawRenderingSettings();
    void drawReflectionSettings();
    void drawShadowSettings();
    void drawDenoiserSettings();
    void drawDebugModes();
    void drawStatistics();

    MiEngine::RayTracingSystem* getRTSystem();
};
