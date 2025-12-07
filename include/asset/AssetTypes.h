#pragma once

#include <string>
#include <cstdint>

namespace MiEngine {

// Asset type classification
enum class AssetType : uint32_t {
    Unknown = 0,
    StaticMesh = 1,
    SkeletalMesh = 2,
    Texture = 3,
    HDR = 4,
    Audio = 5
};

// Convert AssetType to string for display/serialization
inline const char* assetTypeToString(AssetType type) {
    switch (type) {
        case AssetType::StaticMesh: return "StaticMesh";
        case AssetType::SkeletalMesh: return "SkeletalMesh";
        case AssetType::Texture: return "Texture";
        case AssetType::HDR: return "HDR";
        case AssetType::Audio: return "Audio";
        default: return "Unknown";
    }
}

// Parse AssetType from string
inline AssetType stringToAssetType(const std::string& str) {
    if (str == "StaticMesh") return AssetType::StaticMesh;
    if (str == "SkeletalMesh") return AssetType::SkeletalMesh;
    if (str == "Texture") return AssetType::Texture;
    if (str == "HDR") return AssetType::HDR;
    if (str == "Audio") return AssetType::Audio;
    return AssetType::Unknown;
}

// Asset entry in registry
struct AssetEntry {
    std::string uuid;           // Unique identifier
    std::string name;           // Display name (without extension)
    std::string projectPath;    // Relative to project Assets/ (e.g., "Models/robot.fbx")
    std::string cachePath;      // Relative to project Cache/ (e.g., "Models/robot.mimesh")
    AssetType type = AssetType::Unknown;
    uint64_t importTime = 0;    // Unix timestamp when imported
    uint64_t sourceModTime = 0; // Source file modification time at import
    bool cacheValid = false;    // Whether cache is up-to-date
};

// Mesh cache flags (bitfield)
enum class MeshCacheFlags : uint32_t {
    None = 0,
    IsSkeletal = 1 << 0,
    HasAnimations = 1 << 1,
    HasTangents = 1 << 2
};

inline MeshCacheFlags operator|(MeshCacheFlags a, MeshCacheFlags b) {
    return static_cast<MeshCacheFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline MeshCacheFlags operator&(MeshCacheFlags a, MeshCacheFlags b) {
    return static_cast<MeshCacheFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool hasFlag(MeshCacheFlags flags, MeshCacheFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

} // namespace MiEngine
