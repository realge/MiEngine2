#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fbxsdk.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Utils/CommonVertex.h"
#include "Utils/SkeletalVertex.h"

// Forward declarations
namespace MiEngine {
    class Skeleton;
    class AnimationClip;
}

// Structure to hold a mesh's data (static mesh)
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

// Structure to hold skeletal mesh data
struct SkeletalMeshData {
    std::vector<MiEngine::SkeletalVertex> vertices;
    std::vector<unsigned int> indices;
    std::string name;
};

// Complete skeletal model data from FBX
struct SkeletalModelData {
    std::vector<SkeletalMeshData> meshes;
    std::shared_ptr<MiEngine::Skeleton> skeleton;
    std::vector<std::shared_ptr<MiEngine::AnimationClip>> animations;
    bool hasSkeleton = false;
};

class ModelLoader {
public:

    ModelLoader();
    ~ModelLoader();

    // Loads an FBX file and populates internal mesh data (static meshes)
    bool LoadModel(const std::string& filename);

    // Loads an FBX file with skeletal animation data
    bool LoadSkeletalModel(const std::string& filename, SkeletalModelData& outData);

    // Returns the loaded meshes (static)
    const std::vector<MeshData>& GetMeshData() const { return meshes; }

    // Primitive generation
    MeshData CreateSphere(float radius, int slices, int stacks);
    MeshData CreatePlane(float width, float height);
    MeshData CreateCube(float size);

    // Calculate tangents for a mesh
    void CalculateTangents(MeshData& meshData);
    void CalculateTangents(SkeletalMeshData& meshData);

private:
    // Recursively process each node in the FBX scene (static mesh)
    void ProcessNode(FbxNode* node, int indentLevel);

    // Extract mesh data from an FBX mesh node (static)
    void ProcessMesh(FbxMesh* mesh, const FbxAMatrix& transform);

    // Skeletal mesh processing
    void ProcessSkeletalNode(FbxNode* node, SkeletalModelData& outData);
    void ProcessSkeletalMesh(FbxMesh* mesh, const FbxAMatrix& transform, SkeletalModelData& outData);

    // Skeleton extraction
    void ExtractSkeleton(FbxScene* scene, SkeletalModelData& outData);
    void ProcessSkeletonNode(FbxNode* node, int parentIndex, SkeletalModelData& outData);

    // Skinning data extraction
    void ExtractSkinningData(FbxMesh* mesh, SkeletalMeshData& meshData,
                             const SkeletalModelData& modelData);

    // Animation extraction
    void ExtractAnimations(FbxScene* scene, SkeletalModelData& outData);
    void ExtractAnimationStack(FbxAnimStack* animStack, FbxScene* scene,
                               SkeletalModelData& outData);

    // Storage for the meshes loaded from the FBX file
    std::vector<MeshData> meshes;

    // FBX SDK objects for managing the scene
    FbxManager* fbxManager;
    FbxScene* fbxScene;

    // Temporary storage during skeletal loading
    std::unordered_map<std::string, uint32_t> m_boneNameToIndex;
    std::unordered_map<std::string, std::pair<FbxAMatrix, FbxAMatrix>> m_boneClusterData;
    float m_skeletalUnitScale = 1.0f;  // Unit conversion factor for skeletal meshes
};
