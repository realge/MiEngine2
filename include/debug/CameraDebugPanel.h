#pragma once
#include "DebugPanel.h"
#include "camera/Camera.h"
class CameraDebugPanel : public DebugPanel {
public:
    CameraDebugPanel(VulkanRenderer* renderer);
    void draw() override;
    
    // Settings
    void setShowVectors(bool show) { showVectors = show; }
    void setShowSettings(bool show) { showSettings = show; }

private:
    bool showVectors = true;
    bool showSettings = true;
    
    void drawPositionSection();
    void drawRotationSection();
    void drawVectorSection();
    void drawSettingsSection();
    void drawControlsSection();
};
