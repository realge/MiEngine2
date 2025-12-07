#include "asset/AssetBrowserWindow.h"
#include "asset/AssetRegistry.h"
#include "asset/AssetImporter.h"
#include "VulkanRenderer.h"
#include "scene/Scene.h"
#include "imgui.h"
#include <algorithm>

namespace MiEngine {

AssetBrowserWindow::AssetBrowserWindow(VulkanRenderer* renderer)
    : m_renderer(renderer) {
}

void AssetBrowserWindow::draw() {
    if (!m_isOpen) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Asset Browser", &m_isOpen, ImGuiWindowFlags_MenuBar)) {
        drawMenuBar();
        drawToolbar();

        // Main content area
        float footerHeight = 80.0f;
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        contentSize.y -= footerHeight;

        ImGui::BeginChild("AssetListRegion", contentSize, true);
        drawAssetList();
        ImGui::EndChild();

        drawFooter();
    }
    ImGui::End();
}

void AssetBrowserWindow::drawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Import Model...", "Ctrl+I")) {
                handleImport();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Refresh", "F5")) {
                handleRefresh();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("All Assets", nullptr, m_filterType == AssetType::Unknown)) {
                m_filterType = AssetType::Unknown;
                m_needsRefresh = true;
            }
            if (ImGui::MenuItem("Static Meshes", nullptr, m_filterType == AssetType::StaticMesh)) {
                m_filterType = AssetType::StaticMesh;
                m_needsRefresh = true;
            }
            if (ImGui::MenuItem("Skeletal Meshes", nullptr, m_filterType == AssetType::SkeletalMesh)) {
                m_filterType = AssetType::SkeletalMesh;
                m_needsRefresh = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void AssetBrowserWindow::drawToolbar() {
    // Import button
    if (ImGui::Button("Import Model")) {
        handleImport();
    }

    ImGui::SameLine();

    // Refresh button
    if (ImGui::Button("Refresh")) {
        handleRefresh();
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Search box
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputTextWithHint("##search", "Search...", m_searchBuffer, sizeof(m_searchBuffer))) {
        m_searchQuery = m_searchBuffer;
        m_needsRefresh = true;
    }

    ImGui::SameLine();

    // Filter dropdown
    ImGui::SetNextItemWidth(120);
    const char* filterLabels[] = { "All", "Static Mesh", "Skeletal Mesh" };
    int filterIndex = 0;
    if (m_filterType == AssetType::StaticMesh) filterIndex = 1;
    else if (m_filterType == AssetType::SkeletalMesh) filterIndex = 2;

    if (ImGui::Combo("##filter", &filterIndex, filterLabels, 3)) {
        switch (filterIndex) {
            case 0: m_filterType = AssetType::Unknown; break;
            case 1: m_filterType = AssetType::StaticMesh; break;
            case 2: m_filterType = AssetType::SkeletalMesh; break;
        }
        m_needsRefresh = true;
    }

    ImGui::Separator();
}

void AssetBrowserWindow::drawAssetList() {
    if (m_needsRefresh) {
        refreshAssetList();
        m_needsRefresh = false;
    }

    // Table with columns
    if (ImGui::BeginTable("AssetTable", 4,
        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {

        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& entry : m_displayedAssets) {
            ImGui::TableNextRow();

            bool isSelected = (entry.uuid == m_selectedUuid);

            // Name column
            ImGui::TableNextColumn();
            ImGui::PushID(entry.uuid.c_str());

            if (ImGui::Selectable(entry.name.c_str(), isSelected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                m_selectedUuid = entry.uuid;

                // Double-click to add to scene
                if (ImGui::IsMouseDoubleClicked(0)) {
                    handleAddToScene();
                }
            }

            // Context menu
            if (ImGui::BeginPopupContextItem("AssetContextMenu")) {
                if (ImGui::MenuItem("Add to Scene")) {
                    m_selectedUuid = entry.uuid;
                    handleAddToScene();
                }
                if (ImGui::MenuItem("Reimport")) {
                    m_selectedUuid = entry.uuid;
                    handleReimport();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", nullptr, false)) {
                    m_selectedUuid = entry.uuid;
                    handleDelete();
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();

            // Type column
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(assetTypeToString(entry.type));

            // Status column
            ImGui::TableNextColumn();
            if (entry.cacheValid) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Cached");
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Pending");
            }

            // Path column
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", entry.projectPath.c_str());
        }

        ImGui::EndTable();
    }

    if (m_displayedAssets.empty()) {
        ImGui::TextDisabled("No assets found");
        ImGui::TextDisabled("Click 'Import Model' to add assets to your project");
    }
}

void AssetBrowserWindow::drawFooter() {
    ImGui::Separator();

    const AssetEntry* selected = nullptr;
    if (!m_selectedUuid.empty()) {
        selected = AssetRegistry::getInstance().findByUuid(m_selectedUuid);
    }

    if (selected) {
        ImGui::Text("Selected: %s", selected->name.c_str());
        ImGui::TextDisabled("Path: Assets/%s", selected->projectPath.c_str());

        ImGui::Spacing();

        if (ImGui::Button("Add to Scene", ImVec2(120, 0))) {
            handleAddToScene();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reimport", ImVec2(80, 0))) {
            handleReimport();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(60, 0))) {
            handleDelete();
        }
    } else {
        ImGui::TextDisabled("No asset selected");
        ImGui::TextDisabled("Select an asset to see options");
    }

    // Stats line
    ImGui::Spacing();
    auto& registry = AssetRegistry::getInstance();
    ImGui::TextDisabled("Total assets: %zu", registry.getAssetCount());
}

void AssetBrowserWindow::handleImport() {
    std::string uuid = AssetImporter::showImportDialog();
    if (!uuid.empty()) {
        m_selectedUuid = uuid;
        m_needsRefresh = true;
    }
}

void AssetBrowserWindow::handleAddToScene() {
    if (m_selectedUuid.empty() || !m_scene) {
        return;
    }

    const AssetEntry* entry = AssetRegistry::getInstance().findByUuid(m_selectedUuid);
    if (!entry) {
        return;
    }

    // Resolve full path
    auto& registry = AssetRegistry::getInstance();
    std::string fullPath = registry.resolveAssetPath(entry->projectPath).string();

    // Create transform at origin
    Transform transform;
    transform.position = glm::vec3(0.0f);
    transform.rotation = glm::vec3(0.0f);
    transform.scale = glm::vec3(1.0f);

    // Load model into scene
    if (entry->type == AssetType::SkeletalMesh) {
        m_scene->loadSkeletalModel(fullPath, transform);
    } else {
        m_scene->loadModel(fullPath, transform);
    }
}

void AssetBrowserWindow::handleReimport() {
    if (m_selectedUuid.empty()) {
        return;
    }

    AssetImporter::reimport(m_selectedUuid);
    m_needsRefresh = true;
}

void AssetBrowserWindow::handleDelete() {
    if (m_selectedUuid.empty()) {
        return;
    }

    // TODO: Add confirmation dialog
    AssetImporter::deleteAsset(m_selectedUuid);
    m_selectedUuid.clear();
    m_needsRefresh = true;
}

void AssetBrowserWindow::handleRefresh() {
    AssetRegistry::getInstance().refreshAll();
    m_needsRefresh = true;
}

void AssetBrowserWindow::refreshAssetList() {
    m_displayedAssets.clear();

    const auto& allAssets = AssetRegistry::getInstance().getAssets();

    for (const auto& entry : allAssets) {
        // Filter by type
        if (m_filterType != AssetType::Unknown && entry.type != m_filterType) {
            continue;
        }

        // Filter by search query
        if (!m_searchQuery.empty()) {
            std::string nameLower = entry.name;
            std::string queryLower = m_searchQuery;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

            if (nameLower.find(queryLower) == std::string::npos) {
                continue;
            }
        }

        m_displayedAssets.push_back(entry);
    }

    // Sort by name
    std::sort(m_displayedAssets.begin(), m_displayedAssets.end(),
        [](const AssetEntry& a, const AssetEntry& b) {
            return a.name < b.name;
        });
}

const char* AssetBrowserWindow::getStatusText(bool cacheValid) const {
    return cacheValid ? "Cached" : "Pending";
}

const char* AssetBrowserWindow::getTypeIcon(AssetType type) const {
    switch (type) {
        case AssetType::StaticMesh: return "[M]";
        case AssetType::SkeletalMesh: return "[S]";
        case AssetType::Texture: return "[T]";
        default: return "[?]";
    }
}

} // namespace MiEngine
