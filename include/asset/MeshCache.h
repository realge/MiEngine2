#pragma once

#include "AssetTypes.h"
#include "loader/ModelLoader.h"
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace MiEngine {

// Binary cache file header
#pragma pack(push, 1)
struct MeshCacheHeader {
    char magic[8];              // "MIMESH01"
    uint32_t version;           // Format version (1)
    uint32_t flags;             // MeshCacheFlags bitfield
    uint64_t sourceFileHash;    // Hash of source file path for validation
    uint64_t sourceModTime;     // Source file modification time
    uint32_t meshCount;         // Number of submeshes
    uint32_t boneCount;         // Number of bones (0 for static)
    uint32_t animationCount;    // Number of animations (0 for static)
    uint32_t reserved[4];       // Future expansion
};

struct MeshChunkHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t vertexStride;      // sizeof(Vertex) or sizeof(SkeletalVertex)
    float aabbMin[3];           // Bounding box min
    float aabbMax[3];           // Bounding box max
    uint32_t nameLength;        // Length of mesh name (for skeletal)
    uint32_t reserved;
};

struct BoneChunkHeader {
    uint32_t nameLength;
    int32_t parentIndex;
    // Followed by: name string, inverseBindPose (64 bytes), localBindPose (64 bytes)
    // bindPosition (12 bytes), bindRotation (16 bytes), bindScale (12 bytes)
};

struct AnimationChunkHeader {
    uint32_t nameLength;
    float duration;
    float ticksPerSecond;
    uint32_t trackCount;
    uint32_t usesGlobalTransforms;
};

struct TrackChunkHeader {
    uint32_t boneNameLength;
    int32_t boneIndex;
    uint32_t positionKeyCount;
    uint32_t rotationKeyCount;
    uint32_t scaleKeyCount;
    uint32_t matrixKeyCount;
};
#pragma pack(pop)

/**
 * MeshCache handles binary serialization of mesh data for fast loading.
 *
 * File format (.mimesh):
 *   - MeshCacheHeader
 *   - For each mesh:
 *     - MeshChunkHeader
 *     - name string (if skeletal)
 *     - vertex data
 *     - index data
 *   - If skeletal:
 *     - For each bone:
 *       - BoneChunkHeader
 *       - bone data
 *     - For each animation:
 *       - AnimationChunkHeader
 *       - For each track:
 *         - TrackChunkHeader
 *         - keyframe data
 */
class MeshCache {
public:
    static constexpr char MAGIC[] = "MIMESH01";
    static constexpr uint32_t VERSION = 1;

    // Save static mesh data to cache file
    static bool save(const fs::path& cachePath,
                     const std::vector<MeshData>& meshes,
                     const fs::path& sourcePath);

    // Save skeletal mesh data to cache file
    static bool saveSkeletal(const fs::path& cachePath,
                             const SkeletalModelData& data,
                             const fs::path& sourcePath);

    // Load static mesh data from cache
    static bool load(const fs::path& cachePath,
                     std::vector<MeshData>& outMeshes);

    // Load skeletal mesh data from cache
    static bool loadSkeletal(const fs::path& cachePath,
                             SkeletalModelData& outData);

    // Check if cache file is valid and up-to-date
    static bool isValid(const fs::path& cachePath,
                        const fs::path& sourcePath);

    // Generate cache file path from source path
    static fs::path getCachePath(const fs::path& sourcePath,
                                 const fs::path& cacheDir);

    // Get source file hash for comparison
    static uint64_t computeSourceHash(const fs::path& sourcePath);

    // Get source file modification time
    static uint64_t getSourceModTime(const fs::path& sourcePath);

private:
    // Write helpers
    static bool writeHeader(std::ofstream& file, const MeshCacheHeader& header);
    static bool writeMeshChunk(std::ofstream& file, const MeshData& mesh);
    static bool writeSkeletalMeshChunk(std::ofstream& file, const SkeletalMeshData& mesh);
    static bool writeBones(std::ofstream& file, const Skeleton& skeleton);
    static bool writeAnimations(std::ofstream& file,
                                const std::vector<std::shared_ptr<AnimationClip>>& animations);

    // Read helpers
    static bool readHeader(std::ifstream& file, MeshCacheHeader& header);
    static bool readMeshChunk(std::ifstream& file, MeshData& mesh);
    static bool readSkeletalMeshChunk(std::ifstream& file, SkeletalMeshData& mesh);
    static bool readBones(std::ifstream& file, uint32_t boneCount,
                          std::shared_ptr<Skeleton>& skeleton);
    static bool readAnimations(std::ifstream& file, uint32_t animCount,
                               std::vector<std::shared_ptr<AnimationClip>>& animations);
};

} // namespace MiEngine
