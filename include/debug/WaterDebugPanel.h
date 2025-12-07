#pragma once
#include "DebugPanel.h"

class WaterDebugPanel : public DebugPanel {
public:
    WaterDebugPanel(VulkanRenderer* renderer);
    void draw() override;

private:
    // Ripple test parameters
    float rippleStrength = 0.8f;
    float rippleRadius = 0.02f;
    float ripplePosX = 0.5f;
    float ripplePosY = 0.5f;

    // HDR selection
    int currentHDRIndex = 0;

    void drawWaterParameters();
    void drawRippleControls();
    void drawSimulationInfo();
    void drawEnvironmentControls();
};
