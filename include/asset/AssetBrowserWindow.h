#pragma once

#include "AssetTypes.h"
#include <string>
#include <vector>

// Forward declarations
class VulkanRenderer;
class Scene;

namespace MiEngine {

/**
 * AssetBrowserWindow provides a main-menu accessible window
 * for browsing, importing, and managing project assets.
 */
class AssetBrowserWindow {
public:
    AssetBrowserWindow(VulkanRenderer* renderer);
    ~AssetBrowserWindow() = default;

    // Draw the window (call every frame)
    void draw();

    // Visibility control
    bool isOpen() const { return m_isOpen; }
    void open() { m_isOpen = true; }
    void close() { m_isOpen = false; }
    void toggle() { m_isOpen = !m_isOpen; }

    // Set the scene for adding assets
    void setScene(Scene* scene) { m_scene = scene; }

private:
    // UI Sections
    void drawMenuBar();
    void drawToolbar();
    void drawAssetList();
    void drawFooter();
    void drawContextMenu();

    // Actions
    void handleImport();
    void handleAddToScene();
    void handleReimport();
    void handleDelete();
    void handleRefresh();
    void handleGenerateClusteredMesh();
    void handleLoadClusteredMesh();

    // Clustered mesh generation popup
    void drawClusteringPopup();
    void drawClusteredMeshInfoPopup();

    // Helpers
    void refreshAssetList();
    const char* getStatusText(bool cacheValid) const;
    const char* getTypeIcon(AssetType type) const;
    bool hasClusteredMeshCache(const std::string& assetName) const;
    std::string getClusteredMeshCachePath(const std::string& assetName) const;

    VulkanRenderer* m_renderer;
    Scene* m_scene = nullptr;

    bool m_isOpen = false;
    std::string m_selectedUuid;
    AssetType m_filterType = AssetType::Unknown;  // Unknown = All
    std::string m_searchQuery;
    char m_searchBuffer[256] = "";

    // Cached list for display (after filtering)
    std::vector<AssetEntry> m_displayedAssets;
    bool m_needsRefresh = true;

    // Clustering popup state
    bool m_showClusteringPopup = false;
    std::string m_clusteringAssetUuid;
    int m_clusterSize = 128;
    int m_maxLodLevels = 8;
    bool m_generateDebugColors = true;

    // Clustered mesh info popup state
    bool m_showClusteredMeshInfo = false;
    std::string m_clusteredMeshInfoUuid;
};

} // namespace MiEngine
