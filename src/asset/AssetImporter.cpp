#include "asset/AssetImporter.h"
#include "asset/AssetRegistry.h"
#include "asset/MeshCache.h"
#include "loader/ModelLoader.h"
#include "project/ProjectManager.h"
#include <iostream>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#endif

namespace MiEngine {

AssetType AssetImporter::detectAssetType(const fs::path& filePath) {
    std::string ext = filePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb") {
        // Could be static or skeletal - will determine during loading
        return AssetType::StaticMesh;
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
        return AssetType::Texture;
    }
    if (ext == ".hdr" || ext == ".exr") {
        return AssetType::HDR;
    }
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
        return AssetType::Audio;
    }

    return AssetType::Unknown;
}

bool AssetImporter::isSupportedFormat(const fs::path& filePath) {
    std::string ext = filePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Currently only FBX models are fully supported
    return ext == ".fbx";
}

bool AssetImporter::copyToProject(const fs::path& source, const fs::path& destination) {
    try {
        fs::create_directories(destination.parent_path());
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "AssetImporter: Failed to copy file: " << e.what() << std::endl;
        return false;
    }
}

std::string AssetImporter::getRelativeProjectPath(const fs::path& absolutePath,
                                                   const fs::path& projectRoot) {
    fs::path assetsPath = projectRoot / "Assets";
    fs::path relative = fs::relative(absolutePath, assetsPath);
    // Convert to forward slashes for consistency
    std::string result = relative.string();
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

bool AssetImporter::generateCache(const AssetEntry& entry) {
    auto& registry = AssetRegistry::getInstance();

    // Check if registry has project path set
    if (registry.getProjectPath().empty()) {
        std::cerr << "AssetImporter: Registry project path not set!" << std::endl;
        return false;
    }

    fs::path sourcePath = registry.resolveAssetPath(entry.projectPath);
    fs::path cachePath = registry.resolveCachePath(entry.cachePath);

    // Normalize paths to use native separators
    sourcePath = sourcePath.make_preferred();
    cachePath = cachePath.make_preferred();

    std::cout << "AssetImporter: Generating cache for " << sourcePath << std::endl;
    std::cout << "AssetImporter: Cache path: " << cachePath << std::endl;

    // Verify source exists
    if (!fs::exists(sourcePath)) {
        std::cerr << "AssetImporter: Source file not found: " << sourcePath << std::endl;
        return false;
    }

    // Ensure cache directory exists
    fs::create_directories(cachePath.parent_path());

    ModelLoader loader;

    if (entry.type == AssetType::SkeletalMesh) {
        SkeletalModelData modelData;
        if (!loader.LoadSkeletalModel(sourcePath.string(), modelData)) {
            std::cerr << "AssetImporter: Failed to load skeletal model: " << sourcePath << std::endl;
            return false;
        }

        if (!MeshCache::saveSkeletal(cachePath, modelData, sourcePath)) {
            std::cerr << "AssetImporter: Failed to save skeletal cache: " << cachePath << std::endl;
            return false;
        }
    } else {
        // Try as static mesh first
        if (!loader.LoadModel(sourcePath.string())) {
            std::cerr << "AssetImporter: Failed to load model: " << sourcePath << std::endl;
            return false;
        }

        const auto& meshes = loader.GetMeshData();
        if (!MeshCache::save(cachePath, meshes, sourcePath)) {
            std::cerr << "AssetImporter: Failed to save cache: " << cachePath << std::endl;
            return false;
        }
    }

    std::cout << "AssetImporter: Cache generated successfully" << std::endl;
    return true;
}

std::string AssetImporter::importModel(const fs::path& sourceFile) {
    if (!fs::exists(sourceFile)) {
        std::cerr << "AssetImporter: Source file not found: " << sourceFile << std::endl;
        return "";
    }

    if (!isSupportedFormat(sourceFile)) {
        std::cerr << "AssetImporter: Unsupported format: " << sourceFile.extension() << std::endl;
        return "";
    }

    auto& pm = ProjectManager::getInstance();
    if (!pm.hasProject()) {
        std::cerr << "AssetImporter: No project open" << std::endl;
        return "";
    }

    auto& registry = AssetRegistry::getInstance();
    fs::path projectPath = pm.getCurrentProject()->getProjectPath();

    // Determine destination path
    fs::path destDir = projectPath / "Assets" / "Models";
    fs::path destFile = destDir / sourceFile.filename();

    // Copy file to project
    if (!copyToProject(sourceFile, destFile)) {
        return "";
    }

    // Create asset entry
    AssetEntry entry;
    entry.uuid = AssetRegistry::generateUuid();
    entry.name = sourceFile.stem().string();
    entry.projectPath = "Models/" + sourceFile.filename().string();
    entry.cachePath = "Models/" + sourceFile.stem().string() + ".mimesh";

    // Detect if skeletal (need to actually parse to know for sure)
    // Normalize destination path for FBX loader
    fs::path normalizedDest = destFile.make_preferred();
    ModelLoader loader;
    SkeletalModelData skeletalData;
    if (loader.LoadSkeletalModel(normalizedDest.string(), skeletalData) && skeletalData.hasSkeleton) {
        entry.type = AssetType::SkeletalMesh;
    } else {
        entry.type = AssetType::StaticMesh;
    }

    // Set timestamps
    auto now = std::chrono::system_clock::now();
    entry.importTime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    entry.sourceModTime = MeshCache::getSourceModTime(destFile);
    entry.cacheValid = false;

    // Generate cache
    if (generateCache(entry)) {
        entry.cacheValid = true;
    }

    // Register asset
    registry.addAsset(entry);
    registry.save();

    std::cout << "AssetImporter: Imported " << entry.name << " as "
              << assetTypeToString(entry.type) << std::endl;

    return entry.uuid;
}

void AssetImporter::importModelAsync(const fs::path& sourceFile, ImportCallback callback) {
    // For now, just call synchronous version
    // Future: run in separate thread
    std::string uuid = importModel(sourceFile);
    if (callback) {
        callback(!uuid.empty(), uuid, uuid.empty() ? "Import failed" : "");
    }
}

bool AssetImporter::reimport(const std::string& uuid) {
    auto& registry = AssetRegistry::getInstance();
    const AssetEntry* entry = registry.findByUuid(uuid);

    if (!entry) {
        std::cerr << "AssetImporter: Asset not found: " << uuid << std::endl;
        return false;
    }

    // Make a copy since we'll modify it
    AssetEntry updatedEntry = *entry;

    // Update modification time
    fs::path sourcePath = registry.resolveAssetPath(entry->projectPath);
    updatedEntry.sourceModTime = MeshCache::getSourceModTime(sourcePath);

    // Regenerate cache
    if (generateCache(updatedEntry)) {
        updatedEntry.cacheValid = true;
        registry.updateAsset(updatedEntry);
        registry.save();
        std::cout << "AssetImporter: Reimported " << entry->name << std::endl;
        return true;
    }

    registry.invalidateCache(uuid);
    registry.save();
    return false;
}

bool AssetImporter::deleteAsset(const std::string& uuid) {
    auto& registry = AssetRegistry::getInstance();
    const AssetEntry* entry = registry.findByUuid(uuid);

    if (!entry) {
        std::cerr << "AssetImporter: Asset not found: " << uuid << std::endl;
        return false;
    }

    std::string name = entry->name;

    // Delete source file
    fs::path sourcePath = registry.resolveAssetPath(entry->projectPath);
    if (fs::exists(sourcePath)) {
        try {
            fs::remove(sourcePath);
        } catch (const std::exception& e) {
            std::cerr << "AssetImporter: Failed to delete source: " << e.what() << std::endl;
        }
    }

    // Delete cache file
    fs::path cachePath = registry.resolveCachePath(entry->cachePath);
    if (fs::exists(cachePath)) {
        try {
            fs::remove(cachePath);
        } catch (const std::exception& e) {
            std::cerr << "AssetImporter: Failed to delete cache: " << e.what() << std::endl;
        }
    }

    // Remove from registry
    registry.removeAsset(uuid);
    registry.save();

    std::cout << "AssetImporter: Deleted asset " << name << std::endl;
    return true;
}

std::string AssetImporter::showImportDialog() {
#ifdef _WIN32
    wchar_t filename[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"FBX Models (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Import Model";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"fbx";

    if (GetOpenFileNameW(&ofn)) {
        // Convert to UTF-8
        int size = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], size, nullptr, nullptr);

        return importModel(result);
    }
#endif
    return "";
}

} // namespace MiEngine
