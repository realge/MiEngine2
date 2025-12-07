#pragma once
#include "DebugPanel.h"

class RenderDebugPanel : public DebugPanel {
public:
    RenderDebugPanel(VulkanRenderer* renderer);
    void draw() override;
    
private:
    void drawRenderModeSection();
    void drawSceneStatistics();
    void drawViewportInfo();
    void drawPipelineInfo();
    void drawLightDebug();
};
