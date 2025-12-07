#include "debug/PerformancePanel.h"
#include "VulkanRenderer.h"

#include <imgui.h>

PerformancePanel::PerformancePanel(VulkanRenderer* renderer)
    : DebugPanel("Performance", renderer) {
}

void PerformancePanel::updateFrameTime(float deltaTime) {
    frameTimeHistory[historyIndex] = deltaTime * 1000.0f; // Convert to ms
    fpsHistory[historyIndex] = deltaTime > 0 ? 1.0f / deltaTime : 0.0f;
    
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    
    // Calculate statistics
    avgFrameTime = 0.0f;
    minFrameTime = FLT_MAX;
    maxFrameTime = 0.0f;
    
    for (int i = 0; i < HISTORY_SIZE; i++) {
        avgFrameTime += frameTimeHistory[i];
        minFrameTime = std::min(minFrameTime, frameTimeHistory[i]);
        maxFrameTime = std::max(maxFrameTime, frameTimeHistory[i]);
    }
    avgFrameTime /= HISTORY_SIZE;
}

void PerformancePanel::draw() {
    ImGui::SetNextWindowPos(ImVec2(370, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(panelName.c_str(), &isOpen)) {
        drawFPSGraph();
        drawFrameTimeGraph();
        ImGui::Separator();
        drawStatistics();
        ImGui::Separator();
        drawRenderStats();
    }
    ImGui::End();
}

void PerformancePanel::drawFPSGraph() {
    ImGui::Text("FPS:");
    ImGui::PlotLines("##FPS", fpsHistory, HISTORY_SIZE, historyIndex, 
                     nullptr, 0.0f, 120.0f, ImVec2(0, 60));
}

void PerformancePanel::drawFrameTimeGraph() {
    ImGui::Text("Frame Time (ms):");
    ImGui::PlotLines("##FrameTime", frameTimeHistory, HISTORY_SIZE, historyIndex,
                     nullptr, 0.0f, 50.0f, ImVec2(0, 60));
}

void PerformancePanel::drawStatistics() {
    ImGui::Text("Statistics:");
    ImGui::Indent();
    ImGui::Text("Current FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Avg Frame Time: %.2f ms", avgFrameTime);
    ImGui::Text("Min Frame Time: %.2f ms", minFrameTime);
    ImGui::Text("Max Frame Time: %.2f ms", maxFrameTime);
    ImGui::Unindent();
}

void PerformancePanel::drawRenderStats() {
    ImGui::Text("Render Stats:");
    ImGui::Indent();

    const RenderStats& stats = renderer->getRenderStats();

    ImGui::Text("Draw Calls: %u", stats.drawCalls);
    ImGui::Text("Triangles: %u", stats.triangles);
    ImGui::Text("Vertices: %u", stats.vertices);

    // Format large numbers with K/M suffixes
    if (stats.triangles >= 1000000) {
        ImGui::Text("  (%.2f M tris)", stats.triangles / 1000000.0f);
    } else if (stats.triangles >= 1000) {
        ImGui::Text("  (%.1f K tris)", stats.triangles / 1000.0f);
    }

    ImGui::Unindent();
}
