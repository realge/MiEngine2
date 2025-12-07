#include "debug/ScenePanel.h"
#include "VulkanRenderer.h"
#include "project/ProjectManager.h"
#include "core/MiWorld.h"
#include "core/MiActor.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cstring>

ScenePanel::ScenePanel(VulkanRenderer* renderer)
    : DebugPanel("Scene", renderer) {
    refreshSceneList();
}

void ScenePanel::draw() {
    if (!isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene Manager", &isOpen)) {
        // Update status timer
        if (m_StatusTimer > 0.0f) {
            m_StatusTimer -= ImGui::GetIO().DeltaTime;
        }

        // Show status message
        if (m_StatusTimer > 0.0f && !m_StatusMessage.empty()) {
            if (m_StatusIsError) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            }
            ImGui::TextWrapped("%s", m_StatusMessage.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        // Tabs for Save/Load
        if (ImGui::BeginTabBar("SceneTabs")) {
            if (ImGui::BeginTabItem("Save")) {
                drawSaveSection();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Load")) {
                drawLoadSection();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Info")) {
                drawSceneInfo();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void ScenePanel::drawSaveSection() {
    ImGui::Text("Save Current Scene");
    ImGui::Separator();

    // Scene name
    ImGui::Text("Scene Name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##SceneName", m_SceneNameBuffer, sizeof(m_SceneNameBuffer));

    // Description
    ImGui::Text("Description:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##Description", m_DescriptionBuffer, sizeof(m_DescriptionBuffer),
                               ImVec2(0, 60));

    // Author
    ImGui::Text("Author:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##Author", m_AuthorBuffer, sizeof(m_AuthorBuffer));

    ImGui::Spacing();

    // Save path preview
    auto& pm = ProjectManager::getInstance();
    if (pm.hasProject()) {
        fs::path savePath = pm.getCurrentProject()->getScenesPath() /
                           (std::string(m_SceneNameBuffer) + MiEngine::SCENE_FILE_EXTENSION);
        ImGui::TextDisabled("Save to: %s", savePath.string().c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No project open!");
    }

    ImGui::Spacing();

    // Save button
    bool canSave = pm.hasProject() && strlen(m_SceneNameBuffer) > 0;
    if (!canSave) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Save Scene", ImVec2(-1, 30))) {
        saveCurrentScene();
    }

    if (!canSave) {
        ImGui::EndDisabled();
    }
}

void ScenePanel::drawLoadSection() {
    ImGui::Text("Load Scene");
    ImGui::Separator();

    // Refresh button
    if (ImGui::Button("Refresh")) {
        refreshSceneList();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu scenes found)", m_SceneFiles.size());

    ImGui::Spacing();

    // Scene list
    if (ImGui::BeginChild("SceneList", ImVec2(0, 250), true)) {
        for (size_t i = 0; i < m_SceneFiles.size(); ++i) {
            const auto& scenePath = m_SceneFiles[i];
            std::string sceneName = scenePath.stem().string();

            bool isSelected = (static_cast<int>(i) == m_SelectedSceneIndex);
            if (ImGui::Selectable(sceneName.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                m_SelectedSceneIndex = static_cast<int>(i);

                // Double-click to load
                if (ImGui::IsMouseDoubleClicked(0)) {
                    loadSelectedScene();
                }
            }
        }

        if (m_SceneFiles.empty()) {
            ImGui::TextDisabled("No scenes found in project.");
        }
    }
    ImGui::EndChild();

    // Selected scene info
    if (m_SelectedSceneIndex >= 0 && m_SelectedSceneIndex < static_cast<int>(m_SceneFiles.size())) {
        const auto& selectedPath = m_SceneFiles[m_SelectedSceneIndex];

        ImGui::Spacing();
        ImGui::Text("Selected: %s", selectedPath.filename().string().c_str());

        // Peek at metadata
        auto peekResult = MiEngine::SceneSerializer::peekScene(selectedPath);
        if (peekResult.success) {
            ImGui::TextDisabled("Description: %s", peekResult.metadata.description.c_str());
            ImGui::TextDisabled("Author: %s", peekResult.metadata.author.c_str());
        }

        ImGui::Spacing();

        // Load button
        if (ImGui::Button("Load Scene", ImVec2(-1, 30))) {
            loadSelectedScene();
        }
    }
}

void ScenePanel::drawSceneInfo() {
    ImGui::Text("Current Scene Info");
    ImGui::Separator();

    if (!renderer) {
        ImGui::TextDisabled("No renderer available.");
        return;
    }

    MiEngine::MiWorld* world = renderer->getWorld();
    if (!world) {
        ImGui::TextDisabled("No world loaded.");
        return;
    }

    // World info
    ImGui::Text("World Name: %s", world->getName().c_str());
    ImGui::Text("Actor Count: %zu", world->getActorCount());
    ImGui::Text("Is Playing: %s", world->isPlaying() ? "Yes" : "No");
    ImGui::Text("Has Unsaved Changes: %s", world->hasUnsavedChanges() ? "Yes" : "No");

    ImGui::Separator();

    // World settings
    const auto& settings = world->getSettings();
    ImGui::Text("World Settings:");
    ImGui::BulletText("Gravity: (%.1f, %.1f, %.1f)",
                      settings.gravity.x, settings.gravity.y, settings.gravity.z);
    ImGui::BulletText("Physics Enabled: %s", settings.enablePhysics ? "Yes" : "No");
    ImGui::BulletText("Physics Timestep: %.4f", settings.physicsTimeStep);

    ImGui::Separator();

    // Actor list summary
    ImGui::Text("Actors:");
    if (ImGui::BeginChild("ActorList", ImVec2(0, 150), true)) {
        world->forEachActor([](const MiEngine::MiActor* actor) {
            ImGui::BulletText("%s (%s)", actor->getName().c_str(), actor->getTypeName());
        });
    }
    ImGui::EndChild();
}

void ScenePanel::refreshSceneList() {
    m_SceneFiles.clear();
    m_SelectedSceneIndex = -1;

    auto& pm = ProjectManager::getInstance();
    if (!pm.hasProject()) return;

    fs::path scenesPath = pm.getCurrentProject()->getScenesPath();
    if (!fs::exists(scenesPath)) return;

    for (const auto& entry : fs::directory_iterator(scenesPath)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == MiEngine::SCENE_FILE_EXTENSION) {
            m_SceneFiles.push_back(entry.path());
        }
    }

    // Sort by name
    std::sort(m_SceneFiles.begin(), m_SceneFiles.end());
}

void ScenePanel::saveCurrentScene() {
    auto& pm = ProjectManager::getInstance();
    if (!pm.hasProject()) {
        setStatus("No project open!", true);
        return;
    }

    if (!renderer) {
        setStatus("No renderer available!", true);
        return;
    }

    MiEngine::MiWorld* world = renderer->getWorld();
    if (!world) {
        setStatus("No world to save!", true);
        return;
    }

    // Build save path
    fs::path scenesPath = pm.getCurrentProject()->getScenesPath();

    // Create Scenes directory if needed
    if (!fs::exists(scenesPath)) {
        fs::create_directories(scenesPath);
    }

    fs::path savePath = scenesPath / (std::string(m_SceneNameBuffer) + MiEngine::SCENE_FILE_EXTENSION);

    // Build metadata
    MiEngine::SceneMetadata metadata;
    metadata.name = m_SceneNameBuffer;
    metadata.description = m_DescriptionBuffer;
    metadata.author = m_AuthorBuffer;

    // Save
    auto result = MiEngine::SceneSerializer::saveScene(*world, savePath, metadata);

    if (result.success) {
        setStatus("Scene saved: " + savePath.filename().string(), false);
        pm.getCurrentProject()->addRecentScene(savePath.string());
        refreshSceneList();
    } else {
        setStatus("Save failed: " + result.errorMessage, true);
    }
}

void ScenePanel::loadSelectedScene() {
    if (m_SelectedSceneIndex < 0 || m_SelectedSceneIndex >= static_cast<int>(m_SceneFiles.size())) {
        setStatus("No scene selected!", true);
        return;
    }

    if (!renderer) {
        setStatus("No renderer available!", true);
        return;
    }

    MiEngine::MiWorld* world = renderer->getWorld();
    if (!world) {
        setStatus("No world available!", true);
        return;
    }

    const auto& scenePath = m_SceneFiles[m_SelectedSceneIndex];

    // Load scene
    auto result = MiEngine::SceneSerializer::loadScene(*world, scenePath);

    if (result.success) {
        setStatus("Loaded: " + scenePath.filename().string() +
                  " (" + std::to_string(result.actorCount) + " actors)", false);

        // Update UI fields
#ifdef _WIN32
        strncpy_s(m_SceneNameBuffer, sizeof(m_SceneNameBuffer), result.metadata.name.c_str(), _TRUNCATE);
        strncpy_s(m_DescriptionBuffer, sizeof(m_DescriptionBuffer), result.metadata.description.c_str(), _TRUNCATE);
        strncpy_s(m_AuthorBuffer, sizeof(m_AuthorBuffer), result.metadata.author.c_str(), _TRUNCATE);
#else
        strncpy(m_SceneNameBuffer, result.metadata.name.c_str(), sizeof(m_SceneNameBuffer) - 1);
        m_SceneNameBuffer[sizeof(m_SceneNameBuffer) - 1] = '\0';
        strncpy(m_DescriptionBuffer, result.metadata.description.c_str(), sizeof(m_DescriptionBuffer) - 1);
        m_DescriptionBuffer[sizeof(m_DescriptionBuffer) - 1] = '\0';
        strncpy(m_AuthorBuffer, result.metadata.author.c_str(), sizeof(m_AuthorBuffer) - 1);
        m_AuthorBuffer[sizeof(m_AuthorBuffer) - 1] = '\0';
#endif

        // Add to recent
        auto& pm = ProjectManager::getInstance();
        if (pm.hasProject()) {
            pm.getCurrentProject()->addRecentScene(scenePath.string());
        }
    } else {
        setStatus("Load failed: " + result.errorMessage, true);
    }
}

void ScenePanel::setStatus(const std::string& message, bool isError) {
    m_StatusMessage = message;
    m_StatusIsError = isError;
    m_StatusTimer = 5.0f;  // Show for 5 seconds
}
