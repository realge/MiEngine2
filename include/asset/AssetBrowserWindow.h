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

    // Helpers
    void refreshAssetList();
    const char* getStatusText(bool cacheValid) const;
    const char* getTypeIcon(AssetType type) const;

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
};

} // namespace MiEngine
