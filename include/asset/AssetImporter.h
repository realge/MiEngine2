#pragma once

#include "AssetTypes.h"
#include <filesystem>
#include <string>
#include <functional>

namespace fs = std::filesystem;

namespace MiEngine {

/**
 * AssetImporter handles importing external files into the project.
 * - Copies source files to project Assets folder
 * - Generates cached binary data
 * - Registers assets with AssetRegistry
 */
class AssetImporter {
public:
    // Import result callback
    using ImportCallback = std::function<void(bool success, const std::string& uuid,
                                               const std::string& error)>;

    // Import a model file (FBX) into the project
    // Returns the UUID of the imported asset, or empty string on failure
    static std::string importModel(const fs::path& sourceFile);

    // Import with callback for async operation (future enhancement)
    static void importModelAsync(const fs::path& sourceFile, ImportCallback callback);

    // Re-import an existing asset (regenerate cache from source)
    static bool reimport(const std::string& uuid);

    // Delete an asset and its cache
    static bool deleteAsset(const std::string& uuid);

    // Show native file dialog and import selected file
    // Returns UUID of imported asset, or empty string if cancelled/failed
    static std::string showImportDialog();

    // Detect asset type from file extension
    static AssetType detectAssetType(const fs::path& filePath);

    // Check if file extension is supported for import
    static bool isSupportedFormat(const fs::path& filePath);

private:
    // Internal helpers
    static bool copyToProject(const fs::path& source, const fs::path& destination);
    static bool generateCache(const AssetEntry& entry);
    static std::string getRelativeProjectPath(const fs::path& absolutePath,
                                               const fs::path& projectRoot);
};

} // namespace MiEngine
