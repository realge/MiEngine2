#pragma once
#include "DebugPanel.h"

class PerformancePanel : public DebugPanel {
public:
    PerformancePanel(VulkanRenderer* renderer);
    void draw() override;

    // Update performance metrics
    void updateFrameTime(float deltaTime);

private:
    static constexpr int HISTORY_SIZE = 120;
    float frameTimeHistory[HISTORY_SIZE] = {0};
    float fpsHistory[HISTORY_SIZE] = {0};
    int historyIndex = 0;

    float avgFrameTime = 0.0f;
    float minFrameTime = FLT_MAX;
    float maxFrameTime = 0.0f;

    void drawFPSGraph();
    void drawFrameTimeGraph();
    void drawStatistics();
    void drawRenderStats();
};
