#include "asset/MeshCache.h"
#include "animation/Skeleton.h"
#include "animation/AnimationClip.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <functional>

namespace MiEngine {

uint64_t MeshCache::computeSourceHash(const fs::path& sourcePath) {
    // Simple hash of the absolute path string
    std::hash<std::string> hasher;
    return static_cast<uint64_t>(hasher(sourcePath.string()));
}

uint64_t MeshCache::getSourceModTime(const fs::path& sourcePath) {
    if (!fs::exists(sourcePath)) {
        return 0;
    }
    auto ftime = fs::last_write_time(sourcePath);
    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::clock_cast<std::chrono::system_clock>(ftime));
    return static_cast<uint64_t>(sctp.time_since_epoch().count());
}

fs::path MeshCache::getCachePath(const fs::path& sourcePath, const fs::path& cacheDir) {
    // Replace extension with .mimesh
    fs::path cacheName = sourcePath.stem();
    cacheName += ".mimesh";
    return cacheDir / cacheName;
}

bool MeshCache::isValid(const fs::path& cachePath, const fs::path& sourcePath) {
    if (!fs::exists(cachePath) || !fs::exists(sourcePath)) {
        return false;
    }

    std::ifstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    MeshCacheHeader header;
    if (!readHeader(file, header)) {
        return false;
    }

    // Check magic and version
    if (std::strncmp(header.magic, MAGIC, 8) != 0 || header.version != VERSION) {
        return false;
    }

    // Check source file hash and modification time
    uint64_t currentHash = computeSourceHash(sourcePath);
    uint64_t currentModTime = getSourceModTime(sourcePath);

    return header.sourceFileHash == currentHash && header.sourceModTime == currentModTime;
}

// ============================================================================
// Write Functions
// ============================================================================

bool MeshCache::writeHeader(std::ofstream& file, const MeshCacheHeader& header) {
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    return file.good();
}

bool MeshCache::writeMeshChunk(std::ofstream& file, const MeshData& mesh) {
    MeshChunkHeader chunk{};
    chunk.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    chunk.indexCount = static_cast<uint32_t>(mesh.indices.size());
    chunk.vertexStride = sizeof(Vertex);
    chunk.nameLength = 0;

    // Compute AABB
    if (!mesh.vertices.empty()) {
        glm::vec3 minPos = mesh.vertices[0].position;
        glm::vec3 maxPos = mesh.vertices[0].position;
        for (const auto& v : mesh.vertices) {
            minPos = glm::min(minPos, v.position);
            maxPos = glm::max(maxPos, v.position);
        }
        chunk.aabbMin[0] = minPos.x;
        chunk.aabbMin[1] = minPos.y;
        chunk.aabbMin[2] = minPos.z;
        chunk.aabbMax[0] = maxPos.x;
        chunk.aabbMax[1] = maxPos.y;
        chunk.aabbMax[2] = maxPos.z;
    }

    file.write(reinterpret_cast<const char*>(&chunk), sizeof(chunk));

    // Write vertex data
    if (chunk.vertexCount > 0) {
        file.write(reinterpret_cast<const char*>(mesh.vertices.data()),
                   chunk.vertexCount * sizeof(Vertex));
    }

    // Write index data
    if (chunk.indexCount > 0) {
        file.write(reinterpret_cast<const char*>(mesh.indices.data()),
                   chunk.indexCount * sizeof(unsigned int));
    }

    return file.good();
}

bool MeshCache::writeSkeletalMeshChunk(std::ofstream& file, const SkeletalMeshData& mesh) {
    MeshChunkHeader chunk{};
    chunk.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    chunk.indexCount = static_cast<uint32_t>(mesh.indices.size());
    chunk.vertexStride = sizeof(SkeletalVertex);
    chunk.nameLength = static_cast<uint32_t>(mesh.name.length());

    // Compute AABB
    if (!mesh.vertices.empty()) {
        glm::vec3 minPos = mesh.vertices[0].position;
        glm::vec3 maxPos = mesh.vertices[0].position;
        for (const auto& v : mesh.vertices) {
            minPos = glm::min(minPos, v.position);
            maxPos = glm::max(maxPos, v.position);
        }
        chunk.aabbMin[0] = minPos.x;
        chunk.aabbMin[1] = minPos.y;
        chunk.aabbMin[2] = minPos.z;
        chunk.aabbMax[0] = maxPos.x;
        chunk.aabbMax[1] = maxPos.y;
        chunk.aabbMax[2] = maxPos.z;
    }

    file.write(reinterpret_cast<const char*>(&chunk), sizeof(chunk));

    // Write name
    if (chunk.nameLength > 0) {
        file.write(mesh.name.data(), chunk.nameLength);
    }

    // Write vertex data
    if (chunk.vertexCount > 0) {
        file.write(reinterpret_cast<const char*>(mesh.vertices.data()),
                   chunk.vertexCount * sizeof(SkeletalVertex));
    }

    // Write index data
    if (chunk.indexCount > 0) {
        file.write(reinterpret_cast<const char*>(mesh.indices.data()),
                   chunk.indexCount * sizeof(unsigned int));
    }

    return file.good();
}

bool MeshCache::writeBones(std::ofstream& file, const Skeleton& skeleton) {
    for (uint32_t i = 0; i < skeleton.getBoneCount(); ++i) {
        const Bone& bone = skeleton.getBone(i);

        BoneChunkHeader boneHeader{};
        boneHeader.nameLength = static_cast<uint32_t>(bone.name.length());
        boneHeader.parentIndex = bone.parentIndex;

        file.write(reinterpret_cast<const char*>(&boneHeader), sizeof(boneHeader));

        // Write bone name
        if (boneHeader.nameLength > 0) {
            file.write(bone.name.data(), boneHeader.nameLength);
        }

        // Write matrices and vectors
        file.write(reinterpret_cast<const char*>(&bone.inverseBindPose), sizeof(glm::mat4));
        file.write(reinterpret_cast<const char*>(&bone.localBindPose), sizeof(glm::mat4));
        file.write(reinterpret_cast<const char*>(&bone.bindPosition), sizeof(glm::vec3));
        file.write(reinterpret_cast<const char*>(&bone.bindRotation), sizeof(glm::quat));
        file.write(reinterpret_cast<const char*>(&bone.bindScale), sizeof(glm::vec3));
    }

    return file.good();
}

bool MeshCache::writeAnimations(std::ofstream& file,
                                 const std::vector<std::shared_ptr<AnimationClip>>& animations) {
    for (const auto& anim : animations) {
        AnimationChunkHeader animHeader{};
        animHeader.nameLength = static_cast<uint32_t>(anim->getName().length());
        animHeader.duration = anim->getDuration();
        animHeader.ticksPerSecond = anim->getTicksPerSecond();
        animHeader.trackCount = static_cast<uint32_t>(anim->getTracks().size());
        animHeader.usesGlobalTransforms = anim->usesGlobalTransforms() ? 1 : 0;

        file.write(reinterpret_cast<const char*>(&animHeader), sizeof(animHeader));

        // Write animation name
        if (animHeader.nameLength > 0) {
            file.write(anim->getName().data(), animHeader.nameLength);
        }

        // Write tracks
        for (const auto& track : anim->getTracks()) {
            TrackChunkHeader trackHeader{};
            trackHeader.boneNameLength = static_cast<uint32_t>(track.boneName.length());
            trackHeader.boneIndex = track.boneIndex;
            trackHeader.positionKeyCount = static_cast<uint32_t>(track.positionKeys.size());
            trackHeader.rotationKeyCount = static_cast<uint32_t>(track.rotationKeys.size());
            trackHeader.scaleKeyCount = static_cast<uint32_t>(track.scaleKeys.size());
            trackHeader.matrixKeyCount = static_cast<uint32_t>(track.matrixKeys.size());

            file.write(reinterpret_cast<const char*>(&trackHeader), sizeof(trackHeader));

            // Write bone name
            if (trackHeader.boneNameLength > 0) {
                file.write(track.boneName.data(), trackHeader.boneNameLength);
            }

            // Write keyframes (time + value)
            for (const auto& key : track.positionKeys) {
                file.write(reinterpret_cast<const char*>(&key.time), sizeof(float));
                file.write(reinterpret_cast<const char*>(&key.value), sizeof(glm::vec3));
            }
            for (const auto& key : track.rotationKeys) {
                file.write(reinterpret_cast<const char*>(&key.time), sizeof(float));
                file.write(reinterpret_cast<const char*>(&key.value), sizeof(glm::quat));
            }
            for (const auto& key : track.scaleKeys) {
                file.write(reinterpret_cast<const char*>(&key.time), sizeof(float));
                file.write(reinterpret_cast<const char*>(&key.value), sizeof(glm::vec3));
            }
            for (const auto& key : track.matrixKeys) {
                file.write(reinterpret_cast<const char*>(&key.time), sizeof(float));
                file.write(reinterpret_cast<const char*>(&key.value), sizeof(glm::mat4));
            }
        }
    }

    return file.good();
}

bool MeshCache::save(const fs::path& cachePath, const std::vector<MeshData>& meshes,
                      const fs::path& sourcePath) {
    // Ensure cache directory exists
    fs::create_directories(cachePath.parent_path());

    std::ofstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "MeshCache: Failed to create cache file: " << cachePath << std::endl;
        return false;
    }

    // Prepare header
    MeshCacheHeader header{};
    std::memcpy(header.magic, MAGIC, 8);
    header.version = VERSION;
    header.flags = static_cast<uint32_t>(MeshCacheFlags::HasTangents);
    header.sourceFileHash = computeSourceHash(sourcePath);
    header.sourceModTime = getSourceModTime(sourcePath);
    header.meshCount = static_cast<uint32_t>(meshes.size());
    header.boneCount = 0;
    header.animationCount = 0;

    if (!writeHeader(file, header)) {
        return false;
    }

    // Write mesh chunks
    for (const auto& mesh : meshes) {
        if (!writeMeshChunk(file, mesh)) {
            return false;
        }
    }

    std::cout << "MeshCache: Saved " << meshes.size() << " mesh(es) to " << cachePath << std::endl;
    return true;
}

bool MeshCache::saveSkeletal(const fs::path& cachePath, const SkeletalModelData& data,
                              const fs::path& sourcePath) {
    // Ensure cache directory exists
    fs::create_directories(cachePath.parent_path());

    std::ofstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "MeshCache: Failed to create cache file: " << cachePath << std::endl;
        return false;
    }

    // Prepare header
    MeshCacheHeader header{};
    std::memcpy(header.magic, MAGIC, 8);
    header.version = VERSION;
    header.flags = static_cast<uint32_t>(MeshCacheFlags::IsSkeletal | MeshCacheFlags::HasTangents);
    if (!data.animations.empty()) {
        header.flags |= static_cast<uint32_t>(MeshCacheFlags::HasAnimations);
    }
    header.sourceFileHash = computeSourceHash(sourcePath);
    header.sourceModTime = getSourceModTime(sourcePath);
    header.meshCount = static_cast<uint32_t>(data.meshes.size());
    header.boneCount = data.skeleton ? data.skeleton->getBoneCount() : 0;
    header.animationCount = static_cast<uint32_t>(data.animations.size());

    if (!writeHeader(file, header)) {
        return false;
    }

    // Write mesh chunks
    for (const auto& mesh : data.meshes) {
        if (!writeSkeletalMeshChunk(file, mesh)) {
            return false;
        }
    }

    // Write bones
    if (data.skeleton && header.boneCount > 0) {
        if (!writeBones(file, *data.skeleton)) {
            return false;
        }
    }

    // Write animations
    if (!data.animations.empty()) {
        if (!writeAnimations(file, data.animations)) {
            return false;
        }
    }

    std::cout << "MeshCache: Saved skeletal model (" << data.meshes.size() << " meshes, "
              << header.boneCount << " bones, " << header.animationCount << " anims) to "
              << cachePath << std::endl;
    return true;
}

// ============================================================================
// Read Functions
// ============================================================================

bool MeshCache::readHeader(std::ifstream& file, MeshCacheHeader& header) {
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    return file.good();
}

bool MeshCache::readMeshChunk(std::ifstream& file, MeshData& mesh) {
    MeshChunkHeader chunk{};
    file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));
    if (!file.good()) return false;

    // Read vertices
    mesh.vertices.resize(chunk.vertexCount);
    if (chunk.vertexCount > 0) {
        file.read(reinterpret_cast<char*>(mesh.vertices.data()),
                  chunk.vertexCount * sizeof(Vertex));
    }

    // Read indices
    mesh.indices.resize(chunk.indexCount);
    if (chunk.indexCount > 0) {
        file.read(reinterpret_cast<char*>(mesh.indices.data()),
                  chunk.indexCount * sizeof(unsigned int));
    }

    return file.good();
}

bool MeshCache::readSkeletalMeshChunk(std::ifstream& file, SkeletalMeshData& mesh) {
    MeshChunkHeader chunk{};
    file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));
    if (!file.good()) return false;

    // Read name
    if (chunk.nameLength > 0) {
        mesh.name.resize(chunk.nameLength);
        file.read(&mesh.name[0], chunk.nameLength);
    }

    // Read vertices
    mesh.vertices.resize(chunk.vertexCount);
    if (chunk.vertexCount > 0) {
        file.read(reinterpret_cast<char*>(mesh.vertices.data()),
                  chunk.vertexCount * sizeof(SkeletalVertex));
    }

    // Read indices
    mesh.indices.resize(chunk.indexCount);
    if (chunk.indexCount > 0) {
        file.read(reinterpret_cast<char*>(mesh.indices.data()),
                  chunk.indexCount * sizeof(unsigned int));
    }

    return file.good();
}

bool MeshCache::readBones(std::ifstream& file, uint32_t boneCount,
                           std::shared_ptr<Skeleton>& skeleton) {
    skeleton = std::make_shared<Skeleton>();

    for (uint32_t i = 0; i < boneCount; ++i) {
        BoneChunkHeader boneHeader{};
        file.read(reinterpret_cast<char*>(&boneHeader), sizeof(boneHeader));
        if (!file.good()) return false;

        std::string boneName;
        if (boneHeader.nameLength > 0) {
            boneName.resize(boneHeader.nameLength);
            file.read(&boneName[0], boneHeader.nameLength);
        }

        glm::mat4 inverseBindPose, localBindPose;
        glm::vec3 bindPosition, bindScale;
        glm::quat bindRotation;

        file.read(reinterpret_cast<char*>(&inverseBindPose), sizeof(glm::mat4));
        file.read(reinterpret_cast<char*>(&localBindPose), sizeof(glm::mat4));
        file.read(reinterpret_cast<char*>(&bindPosition), sizeof(glm::vec3));
        file.read(reinterpret_cast<char*>(&bindRotation), sizeof(glm::quat));
        file.read(reinterpret_cast<char*>(&bindScale), sizeof(glm::vec3));

        if (!file.good()) return false;

        // Add bone to skeleton
        uint32_t boneIndex = skeleton->addBone(boneName, boneHeader.parentIndex,
                                                inverseBindPose, localBindPose);

        // Set decomposed bind pose
        Bone& bone = skeleton->getBone(boneIndex);
        bone.bindPosition = bindPosition;
        bone.bindRotation = bindRotation;
        bone.bindScale = bindScale;
    }

    return true;
}

bool MeshCache::readAnimations(std::ifstream& file, uint32_t animCount,
                                std::vector<std::shared_ptr<AnimationClip>>& animations) {
    animations.reserve(animCount);

    for (uint32_t a = 0; a < animCount; ++a) {
        AnimationChunkHeader animHeader{};
        file.read(reinterpret_cast<char*>(&animHeader), sizeof(animHeader));
        if (!file.good()) return false;

        std::string animName;
        if (animHeader.nameLength > 0) {
            animName.resize(animHeader.nameLength);
            file.read(&animName[0], animHeader.nameLength);
        }

        auto clip = std::make_shared<AnimationClip>(animName, animHeader.duration,
                                                     animHeader.ticksPerSecond);
        clip->setUsesGlobalTransforms(animHeader.usesGlobalTransforms != 0);

        // Read tracks
        for (uint32_t t = 0; t < animHeader.trackCount; ++t) {
            TrackChunkHeader trackHeader{};
            file.read(reinterpret_cast<char*>(&trackHeader), sizeof(trackHeader));
            if (!file.good()) return false;

            std::string boneName;
            if (trackHeader.boneNameLength > 0) {
                boneName.resize(trackHeader.boneNameLength);
                file.read(&boneName[0], trackHeader.boneNameLength);
            }

            BoneAnimationTrack& track = clip->addTrack(boneName);
            track.boneIndex = trackHeader.boneIndex;

            // Read position keys
            track.positionKeys.resize(trackHeader.positionKeyCount);
            for (auto& key : track.positionKeys) {
                file.read(reinterpret_cast<char*>(&key.time), sizeof(float));
                file.read(reinterpret_cast<char*>(&key.value), sizeof(glm::vec3));
            }

            // Read rotation keys
            track.rotationKeys.resize(trackHeader.rotationKeyCount);
            for (auto& key : track.rotationKeys) {
                file.read(reinterpret_cast<char*>(&key.time), sizeof(float));
                file.read(reinterpret_cast<char*>(&key.value), sizeof(glm::quat));
            }

            // Read scale keys
            track.scaleKeys.resize(trackHeader.scaleKeyCount);
            for (auto& key : track.scaleKeys) {
                file.read(reinterpret_cast<char*>(&key.time), sizeof(float));
                file.read(reinterpret_cast<char*>(&key.value), sizeof(glm::vec3));
            }

            // Read matrix keys
            track.matrixKeys.resize(trackHeader.matrixKeyCount);
            for (auto& key : track.matrixKeys) {
                file.read(reinterpret_cast<char*>(&key.time), sizeof(float));
                file.read(reinterpret_cast<char*>(&key.value), sizeof(glm::mat4));
            }
        }

        animations.push_back(clip);
    }

    return file.good();
}

bool MeshCache::load(const fs::path& cachePath, std::vector<MeshData>& outMeshes) {
    std::ifstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "MeshCache: Failed to open cache file: " << cachePath << std::endl;
        return false;
    }

    MeshCacheHeader header{};
    if (!readHeader(file, header)) {
        std::cerr << "MeshCache: Failed to read header" << std::endl;
        return false;
    }

    // Validate header
    if (std::strncmp(header.magic, MAGIC, 8) != 0) {
        std::cerr << "MeshCache: Invalid magic number" << std::endl;
        return false;
    }
    if (header.version != VERSION) {
        std::cerr << "MeshCache: Version mismatch (file: " << header.version
                  << ", expected: " << VERSION << ")" << std::endl;
        return false;
    }
    if (hasFlag(static_cast<MeshCacheFlags>(header.flags), MeshCacheFlags::IsSkeletal)) {
        std::cerr << "MeshCache: Expected static mesh, got skeletal" << std::endl;
        return false;
    }

    // Read meshes
    outMeshes.resize(header.meshCount);
    for (uint32_t i = 0; i < header.meshCount; ++i) {
        if (!readMeshChunk(file, outMeshes[i])) {
            std::cerr << "MeshCache: Failed to read mesh chunk " << i << std::endl;
            return false;
        }
    }

    std::cout << "MeshCache: Loaded " << outMeshes.size() << " mesh(es) from " << cachePath << std::endl;
    return true;
}

bool MeshCache::loadSkeletal(const fs::path& cachePath, SkeletalModelData& outData) {
    std::ifstream file(cachePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "MeshCache: Failed to open cache file: " << cachePath << std::endl;
        return false;
    }

    MeshCacheHeader header{};
    if (!readHeader(file, header)) {
        std::cerr << "MeshCache: Failed to read header" << std::endl;
        return false;
    }

    // Validate header
    if (std::strncmp(header.magic, MAGIC, 8) != 0) {
        std::cerr << "MeshCache: Invalid magic number" << std::endl;
        return false;
    }
    if (header.version != VERSION) {
        std::cerr << "MeshCache: Version mismatch" << std::endl;
        return false;
    }
    if (!hasFlag(static_cast<MeshCacheFlags>(header.flags), MeshCacheFlags::IsSkeletal)) {
        std::cerr << "MeshCache: Expected skeletal mesh, got static" << std::endl;
        return false;
    }

    // Read meshes
    outData.meshes.resize(header.meshCount);
    for (uint32_t i = 0; i < header.meshCount; ++i) {
        if (!readSkeletalMeshChunk(file, outData.meshes[i])) {
            std::cerr << "MeshCache: Failed to read skeletal mesh chunk " << i << std::endl;
            return false;
        }
    }

    // Read bones
    if (header.boneCount > 0) {
        if (!readBones(file, header.boneCount, outData.skeleton)) {
            std::cerr << "MeshCache: Failed to read bones" << std::endl;
            return false;
        }
        outData.hasSkeleton = true;
    }

    // Read animations
    if (header.animationCount > 0) {
        if (!readAnimations(file, header.animationCount, outData.animations)) {
            std::cerr << "MeshCache: Failed to read animations" << std::endl;
            return false;
        }
    }

    std::cout << "MeshCache: Loaded skeletal model (" << outData.meshes.size() << " meshes, "
              << header.boneCount << " bones, " << outData.animations.size() << " anims) from "
              << cachePath << std::endl;
    return true;
}

} // namespace MiEngine
