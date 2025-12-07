#pragma once

#include "debug/DebugPanel.h"
#include "scene/SceneSerializer.h"
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

class ScenePanel : public DebugPanel {
public:
    ScenePanel(VulkanRenderer* renderer);

    void draw() override;

private:
    // UI state
    char m_SceneNameBuffer[256] = "NewScene";
    char m_DescriptionBuffer[512] = "";
    char m_AuthorBuffer[128] = "";

    std::string m_StatusMessage;
    bool m_StatusIsError = false;
    float m_StatusTimer = 0.0f;

    // Scene list
    std::vector<fs::path> m_SceneFiles;
    int m_SelectedSceneIndex = -1;

    // Helper methods
    void drawSaveSection();
    void drawLoadSection();
    void drawSceneInfo();

    void refreshSceneList();
    void saveCurrentScene();
    void loadSelectedScene();

    void setStatus(const std::string& message, bool isError = false);
};
