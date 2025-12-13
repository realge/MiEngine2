#include "asset/AssetBrowserWindow.h"
#include "asset/AssetRegistry.h"
#include "asset/AssetImporter.h"
#include "virtualgeo/VirtualGeoTypes.h"
#include "virtualgeo/MeshClusterer.h"
#include "virtualgeo/ClusterDAGBuilder.h"
#include "virtualgeo/ClusteredMeshCache.h"
#include "loader/ModelLoader.h"
#include "VulkanRenderer.h"
#include "scene/Scene.h"
#include "imgui.h"
#include <algorithm>
#include <iostream>
#include <filesystem>

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

    // Draw clustering popup (modal)
    drawClusteringPopup();

    // Draw clustered mesh info popup (modal)
    drawClusteredMeshInfoPopup();
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
                // Only show clustering options for static/skeletal meshes
                if (entry.type == AssetType::StaticMesh || entry.type == AssetType::SkeletalMesh) {
                    bool hasCache = hasClusteredMeshCache(entry.name);

                    if (hasCache) {
                        // Show option to view/load existing cache
                        if (ImGui::MenuItem("View Clustered Mesh Info...")) {
                            m_selectedUuid = entry.uuid;
                            m_clusteredMeshInfoUuid = entry.uuid;
                            m_showClusteredMeshInfo = true;
                        }
                        if (ImGui::MenuItem("Regenerate Clustered Mesh...")) {
                            m_selectedUuid = entry.uuid;
                            handleGenerateClusteredMesh();
                        }
                    } else {
                        if (ImGui::MenuItem("Generate Clustered Mesh...")) {
                            m_selectedUuid = entry.uuid;
                            handleGenerateClusteredMesh();
                        }
                    }
                    ImGui::Separator();
                }
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
            if (entry.type == AssetType::StaticMesh || entry.type == AssetType::SkeletalMesh) {
                bool hasCluster = hasClusteredMeshCache(entry.name);
                if (hasCluster) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Clustered");
                } else if (entry.cacheValid) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Cached");
                } else {
                    ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Pending");
                }
            } else {
                if (entry.cacheValid) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Cached");
                } else {
                    ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Pending");
                }
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
        case AssetType::ClusteredMesh: return "[C]";
        default: return "[?]";
    }
}

void AssetBrowserWindow::handleGenerateClusteredMesh() {
    if (m_selectedUuid.empty()) {
        return;
    }

    const AssetEntry* entry = AssetRegistry::getInstance().findByUuid(m_selectedUuid);
    if (!entry) {
        return;
    }

    // Only allow for static/skeletal meshes
    if (entry->type != AssetType::StaticMesh && entry->type != AssetType::SkeletalMesh) {
        return;
    }

    // Store the UUID and open the popup
    m_clusteringAssetUuid = m_selectedUuid;
    m_showClusteringPopup = true;
}

void AssetBrowserWindow::drawClusteringPopup() {
    if (!m_showClusteringPopup) {
        return;
    }

    ImGui::OpenPopup("Generate Clustered Mesh");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Generate Clustered Mesh", &m_showClusteringPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        const AssetEntry* entry = AssetRegistry::getInstance().findByUuid(m_clusteringAssetUuid);
        if (!entry) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: Asset not found");
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                m_showClusteringPopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        ImGui::Text("Asset: %s", entry->name.c_str());
        ImGui::TextDisabled("Path: %s", entry->projectPath.c_str());
        ImGui::Separator();

        ImGui::Text("Clustering Options:");
        ImGui::Spacing();

        // Cluster size slider
        ImGui::SliderInt("Target Cluster Size", &m_clusterSize, 32, 256);
        ImGui::SameLine();
        if (ImGui::Button("Reset##ClusterSize")) {
            m_clusterSize = 128;
        }
        ImGui::TextDisabled("Triangles per cluster (128 is optimal for GPU)");

        ImGui::Spacing();

        // Max LOD levels
        ImGui::SliderInt("Max LOD Levels", &m_maxLodLevels, 1, 16);
        ImGui::SameLine();
        if (ImGui::Button("Reset##LOD")) {
            m_maxLodLevels = 8;
        }
        ImGui::TextDisabled("Number of LOD levels to generate");

        ImGui::Spacing();

        // Debug colors
        ImGui::Checkbox("Generate Debug Colors", &m_generateDebugColors);
        ImGui::TextDisabled("Assigns unique colors to each cluster for visualization");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Buttons
        float buttonWidth = 120.0f;
        float spacing = 10.0f;
        float totalWidth = buttonWidth * 2 + spacing;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - totalWidth) * 0.5f);

        if (ImGui::Button("Generate", ImVec2(buttonWidth, 0))) {
            // Perform the clustering
            auto& registry = AssetRegistry::getInstance();
            std::filesystem::path sourcePath = registry.resolveAssetPath(entry->projectPath);

            if (std::filesystem::exists(sourcePath)) {
                std::cout << "[ClusteredMesh] Generating clustered mesh for: " << entry->name << std::endl;
                std::cout << "  Source: " << sourcePath.string() << std::endl;
                std::cout << "  Cluster size: " << m_clusterSize << std::endl;
                std::cout << "  Max LOD levels: " << m_maxLodLevels << std::endl;

                // Load the mesh data
                ModelLoader loader;
                if (loader.LoadModel(sourcePath.string())) {
                    const std::vector<MeshData>& meshes = loader.GetMeshData();

                    // Flatten all meshes into single vertex/index arrays
                    std::vector<Vertex> allVertices;
                    std::vector<uint32_t> allIndices;

                    for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
                        const MeshData& meshData = meshes[meshIdx];
                        uint32_t baseVertex = static_cast<uint32_t>(allVertices.size());

                        // Add vertices directly (already Vertex type)
                        for (size_t vi = 0; vi < meshData.vertices.size(); ++vi) {
                            allVertices.push_back(meshData.vertices[vi]);
                        }

                        // Add indices with offset
                        for (size_t ii = 0; ii < meshData.indices.size(); ++ii) {
                            allIndices.push_back(baseVertex + meshData.indices[ii]);
                        }
                    }

                    std::cout << "  Total vertices: " << allVertices.size() << std::endl;
                    std::cout << "  Total triangles: " << allIndices.size() / 3 << std::endl;

                    // Perform clustering
                    MeshClusterer clusterer;
                    ClusteringOptions options;
                    options.targetClusterSize = static_cast<uint32_t>(m_clusterSize);
                    options.maxLodLevels = static_cast<uint32_t>(m_maxLodLevels);
                    options.generateDebugColors = m_generateDebugColors;
                    options.verbose = true;

                    ClusteredMesh clusteredMesh;
                    if (clusterer.clusterMesh(allVertices, allIndices, options, clusteredMesh)) {
                        // Build LOD hierarchy
                        ClusterDAGBuilder dagBuilder;
                        dagBuilder.buildDAG(clusteredMesh, options);

                        // Save to cache
                        std::filesystem::path cachePath = registry.getCachePath() /
                            (entry->name + ".micluster");

                        if (ClusteredMeshCache::save(cachePath, clusteredMesh, sourcePath)) {
                            std::cout << "[ClusteredMesh] Saved to: " << cachePath.string() << std::endl;
                            std::cout << "[ClusteredMesh] Generation complete!" << std::endl;
                            std::cout << "  Clusters: " << clusteredMesh.clusters.size() << std::endl;
                            std::cout << "  Max LOD: " << clusteredMesh.maxLodLevel << std::endl;
                        } else {
                            std::cerr << "[ClusteredMesh] Error: Failed to save cache file" << std::endl;
                        }
                    } else {
                        std::cerr << "[ClusteredMesh] Error: Clustering failed" << std::endl;
                    }
                } else {
                    std::cerr << "[ClusteredMesh] Error: Failed to load mesh from " << sourcePath.string() << std::endl;
                }
            } else {
                std::cerr << "[ClusteredMesh] Error: Source file not found: " << sourcePath.string() << std::endl;
            }

            m_showClusteringPopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            m_showClusteringPopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void AssetBrowserWindow::drawClusteredMeshInfoPopup() {
    if (!m_showClusteredMeshInfo) {
        return;
    }

    ImGui::OpenPopup("Clustered Mesh Info");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Clustered Mesh Info", &m_showClusteredMeshInfo, ImGuiWindowFlags_AlwaysAutoResize)) {
        const AssetEntry* entry = AssetRegistry::getInstance().findByUuid(m_clusteredMeshInfoUuid);
        if (!entry) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: Asset not found");
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                m_showClusteredMeshInfo = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        std::string cachePath = getClusteredMeshCachePath(entry->name);

        ImGui::Text("Asset: %s", entry->name.c_str());
        ImGui::TextDisabled("Cache: %s", cachePath.c_str());
        ImGui::Separator();

        // Try to load and display info from the cache file
        ClusteredMesh loadedMesh;
        std::filesystem::path cacheFilePath(cachePath);

        if (ClusteredMeshCache::load(cacheFilePath, loadedMesh)) {
            ImGui::Text("Clustered Mesh Statistics:");
            ImGui::Spacing();

            // Count clusters per LOD level
            std::vector<uint32_t> clustersPerLod(loadedMesh.maxLodLevel + 1, 0);
            for (size_t i = 0; i < loadedMesh.clusters.size(); ++i) {
                const Cluster& c = loadedMesh.clusters[i];
                if (c.lodLevel <= loadedMesh.maxLodLevel) {
                    clustersPerLod[c.lodLevel]++;
                }
            }

            ImGui::BulletText("Total Clusters: %zu", loadedMesh.clusters.size());
            ImGui::BulletText("Total Vertices: %zu", loadedMesh.vertices.size());
            ImGui::BulletText("Total Indices: %zu", loadedMesh.indices.size());
            ImGui::BulletText("Max LOD Level: %u", loadedMesh.maxLodLevel);

            ImGui::Spacing();
            ImGui::Text("Clusters per LOD Level:");

            if (ImGui::BeginTable("LODTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableSetupColumn("Clusters", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Triangles (approx)", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (uint32_t lod = 0; lod <= loadedMesh.maxLodLevel; ++lod) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", lod);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", clustersPerLod[lod]);
                    ImGui::TableNextColumn();
                    // Approximate triangles (128 per cluster)
                    ImGui::Text("~%u", clustersPerLod[lod] * 128);
                }

                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Action buttons
            float buttonWidth = 140.0f;

            if (ImGui::Button("Load to Scene", ImVec2(buttonWidth, 0))) {
                // TODO: Implement loading clustered mesh to scene
                std::cout << "[ClusteredMesh] Loading clustered mesh to scene: " << entry->name << std::endl;
                std::cout << "  Clusters: " << loadedMesh.clusters.size() << std::endl;
                std::cout << "  (Scene integration pending - Milestone 9)" << std::endl;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Cache", ImVec2(buttonWidth, 0))) {
                if (std::filesystem::exists(cacheFilePath)) {
                    std::filesystem::remove(cacheFilePath);
                    std::cout << "[ClusteredMesh] Deleted cache: " << cachePath << std::endl;
                    m_showClusteredMeshInfo = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(80, 0))) {
                m_showClusteredMeshInfo = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: Failed to load cache file");
            ImGui::TextDisabled("The cache file may be corrupted or incompatible.");

            if (ImGui::Button("Close", ImVec2(120, 0))) {
                m_showClusteredMeshInfo = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

void AssetBrowserWindow::handleLoadClusteredMesh() {
    if (m_selectedUuid.empty()) {
        return;
    }

    const AssetEntry* entry = AssetRegistry::getInstance().findByUuid(m_selectedUuid);
    if (!entry) {
        return;
    }

    std::string cachePath = getClusteredMeshCachePath(entry->name);
    std::filesystem::path cacheFilePath(cachePath);

    if (!std::filesystem::exists(cacheFilePath)) {
        std::cerr << "[ClusteredMesh] Cache file not found: " << cachePath << std::endl;
        return;
    }

    ClusteredMesh loadedMesh;
    if (ClusteredMeshCache::load(cacheFilePath, loadedMesh)) {
        std::cout << "[ClusteredMesh] Loaded: " << entry->name << std::endl;
        std::cout << "  Clusters: " << loadedMesh.clusters.size() << std::endl;
        std::cout << "  Max LOD: " << loadedMesh.maxLodLevel << std::endl;
        // TODO: Add to scene via renderer
    } else {
        std::cerr << "[ClusteredMesh] Failed to load cache file: " << cachePath << std::endl;
    }
}

bool AssetBrowserWindow::hasClusteredMeshCache(const std::string& assetName) const {
    std::string cachePath = getClusteredMeshCachePath(assetName);
    return std::filesystem::exists(cachePath);
}

std::string AssetBrowserWindow::getClusteredMeshCachePath(const std::string& assetName) const {
    auto& registry = AssetRegistry::getInstance();
    std::filesystem::path cachePath = registry.getCachePath() / (assetName + ".micluster");
    return cachePath.string();
}

} // namespace MiEngine
