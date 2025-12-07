#pragma once

#include "AssetTypes.h"
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

namespace MiEngine {

/**
 * AssetRegistry tracks all imported assets in a project.
 * Persisted to asset_registry.json in the project root.
 */
class AssetRegistry {
public:
    static AssetRegistry& getInstance();

    // Project lifecycle
    void loadFromProject(const fs::path& projectPath);
    void save();
    void clear();

    // Query methods
    const std::vector<AssetEntry>& getAssets() const { return m_assets; }
    const AssetEntry* findByUuid(const std::string& uuid) const;
    const AssetEntry* findByPath(const std::string& projectPath) const;
    std::vector<AssetEntry> getAssetsByType(AssetType type) const;
    size_t getAssetCount() const { return m_assets.size(); }

    // Modification
    void addAsset(const AssetEntry& entry);
    void updateAsset(const AssetEntry& entry);
    void removeAsset(const std::string& uuid);

    // Cache management
    void invalidateCache(const std::string& uuid);
    void validateCache(const std::string& uuid);
    void refreshAll();  // Re-validate all caches against source files

    // Path helpers
    fs::path getProjectPath() const { return m_projectPath; }
    fs::path getAssetsPath() const { return m_projectPath / "Assets"; }
    fs::path getCachePath() const { return m_projectPath / "Cache"; }
    fs::path getRegistryFilePath() const { return m_projectPath / "asset_registry.json"; }

    // Resolve relative project path to absolute path
    fs::path resolveAssetPath(const std::string& projectPath) const;
    fs::path resolveCachePath(const std::string& cachePath) const;

    // UUID generation
    static std::string generateUuid();

private:
    AssetRegistry() = default;
    ~AssetRegistry() = default;
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    void rebuildIndex();

    std::vector<AssetEntry> m_assets;
    std::unordered_map<std::string, size_t> m_uuidIndex;     // uuid -> index in m_assets
    std::unordered_map<std::string, size_t> m_pathIndex;     // projectPath -> index
    fs::path m_projectPath;
    bool m_dirty = false;
};

} // namespace MiEngine
