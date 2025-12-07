#pragma once
#include "DebugPanel.h"

class SettingsPanel : public DebugPanel {
public:
    SettingsPanel(VulkanRenderer* renderer);
    void draw() override;
    
private:
    void drawCameraSettings();
    void drawRenderSettings();
    void drawDebugSettings();
};
