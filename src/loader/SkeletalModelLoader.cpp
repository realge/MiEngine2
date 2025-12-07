// =====================================================
// SKELETAL MESH LOADING IMPLEMENTATION
// This file is included/compiled separately from ModelLoader.cpp
// =====================================================

#include "loader/ModelLoader.h"
#include "animation/Skeleton.h"
#include "animation/AnimationClip.h"
#include <fbxsdk.h>
#include <iostream>
#include <functional>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

using namespace MiEngine;

// Helper: Convert FbxAMatrix to glm::mat4
static glm::mat4 FbxMatrixToGlm(const FbxAMatrix& fbxMatrix) {
    glm::mat4 result;
    // FBX Get(row, col) - but FBX stores row-major, GLM is column-major
    // So we need: result[col][row] = fbx.Get(row, col)
    // But testing shows we need to transpose, so: result[row][col] = fbx.Get(row, col)
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result[row][col] = static_cast<float>(fbxMatrix.Get(row, col));
        }
    }
    return result;
}

// Helper: Convert FbxQuaternion to glm::quat
static glm::quat FbxQuatToGlm(const FbxQuaternion& q) {
    return glm::quat(static_cast<float>(q[3]),  // w
                     static_cast<float>(q[0]),  // x
                     static_cast<float>(q[1]),  // y
                     static_cast<float>(q[2])); // z
}

bool ModelLoader::LoadSkeletalModel(const std::string& filename, SkeletalModelData& outData) {
    // Create a new scene for skeletal loading
    FbxScene* skeletalScene = FbxScene::Create(fbxManager, "SkeletalScene");
    if (!skeletalScene) {
        std::cerr << "Failed to create FBX scene for skeletal model" << std::endl;
        return false;
    }

    // Create importer
    FbxImporter* importer = FbxImporter::Create(fbxManager, "");
    if (!importer->Initialize(filename.c_str(), -1, fbxManager->GetIOSettings())) {
        std::cerr << "Failed to initialize FBX importer: "
                  << importer->GetStatus().GetErrorString() << std::endl;
        importer->Destroy();
        skeletalScene->Destroy();
        return false;
    }

    // Import the scene
    if (!importer->Import(skeletalScene)) {
        std::cerr << "Failed to import FBX file: " << filename << std::endl;
        importer->Destroy();
        skeletalScene->Destroy();
        return false;
    }
    importer->Destroy();

    // Calculate unit scale factor (FBX files often use centimeters)
    FbxSystemUnit sceneSystemUnit = skeletalScene->GetGlobalSettings().GetSystemUnit();
    m_skeletalUnitScale = static_cast<float>(sceneSystemUnit.GetConversionFactorTo(FbxSystemUnit::m));
    std::cout << "Skeletal unit scale: " << m_skeletalUnitScale
              << " (from " << sceneSystemUnit.GetScaleFactorAsString() << ")" << std::endl;

    // Triangulate the scene
    FbxGeometryConverter geometryConverter(fbxManager);
    geometryConverter.Triangulate(skeletalScene, true);

    // Clear temporary bone map
    m_boneNameToIndex.clear();

    // Extract skeleton first (needed for skinning)
    ExtractSkeleton(skeletalScene, outData);

    // Process all mesh nodes
    FbxNode* rootNode = skeletalScene->GetRootNode();
    if (rootNode) {
        for (int i = 0; i < rootNode->GetChildCount(); i++) {
            ProcessSkeletalNode(rootNode->GetChild(i), outData);
        }
    }

    // Calculate tangents for all meshes
    for (auto& mesh : outData.meshes) {
        CalculateTangents(mesh);
    }

    // Extract animations
    ExtractAnimations(skeletalScene, outData);

    // Bind animations to skeleton
    if (outData.skeleton) {
        for (auto& anim : outData.animations) {
            anim->bindToSkeleton(*outData.skeleton);
        }
    }

    skeletalScene->Destroy();
    return true;
}

void ModelLoader::ProcessSkeletalNode(FbxNode* node, SkeletalModelData& outData) {
    if (!node) return;

    // Calculate global transform
    FbxAMatrix globalTransform = node->EvaluateGlobalTransform();

    // Process mesh if present
    FbxMesh* mesh = node->GetMesh();
    if (mesh) {
        ProcessSkeletalMesh(mesh, globalTransform, outData);
    }

    // Recursively process children
    for (int i = 0; i < node->GetChildCount(); i++) {
        ProcessSkeletalNode(node->GetChild(i), outData);
    }
}

void ModelLoader::ProcessSkeletalMesh(FbxMesh* mesh, const FbxAMatrix& transform,
                                       SkeletalModelData& outData) {
    SkeletalMeshData meshData;
    meshData.name = mesh->GetNode() ? mesh->GetNode()->GetName() : "unnamed";

    FbxVector4* controlPoints = mesh->GetControlPoints();
    int controlPointCount = mesh->GetControlPointsCount();

    // Temporary storage: skinning data per control point
    struct ControlPointSkinData {
        std::vector<std::pair<int, float>> boneInfluences;
    };
    std::vector<ControlPointSkinData> cpSkinData(controlPointCount);

    // Extract skinning data from mesh deformers
    int deformerCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int deformerIndex = 0; deformerIndex < deformerCount; ++deformerIndex) {
        FbxSkin* skin = static_cast<FbxSkin*>(mesh->GetDeformer(deformerIndex, FbxDeformer::eSkin));
        if (!skin) continue;

        int clusterCount = skin->GetClusterCount();

        for (int clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex) {
            FbxCluster* cluster = skin->GetCluster(clusterIndex);
            if (!cluster) continue;

            FbxNode* boneNode = cluster->GetLink();
            if (!boneNode) continue;

            std::string boneName = boneNode->GetName();
            auto it = m_boneNameToIndex.find(boneName);
            if (it == m_boneNameToIndex.end()) {
                std::cerr << "Warning: Bone '" << boneName << "' not found in skeleton" << std::endl;
                continue;
            }
            int boneIndex = static_cast<int>(it->second);

            int* indices = cluster->GetControlPointIndices();
            double* weights = cluster->GetControlPointWeights();
            int indexCount = cluster->GetControlPointIndicesCount();

            for (int i = 0; i < indexCount; ++i) {
                int cpIndex = indices[i];
                float weight = static_cast<float>(weights[i]);
                if (cpIndex >= 0 && cpIndex < controlPointCount && weight > 0.0001f) {
                    cpSkinData[cpIndex].boneInfluences.push_back({boneIndex, weight});
                }
            }
        }
    }

    // Process polygons
    int polygonCount = mesh->GetPolygonCount();
    for (int polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex) {
        if (mesh->GetPolygonSize(polygonIndex) != 3) {
            std::cerr << "Warning: Non-triangulated polygon found" << std::endl;
            continue;
        }

        // Reverse winding order
        int vertexOrder[3] = {2, 1, 0};

        for (int i = 0; i < 3; ++i) {
            int vertexIndex = vertexOrder[i];
            MiEngine::SkeletalVertex vertex;

            int controlPointIndex = mesh->GetPolygonVertex(polygonIndex, vertexIndex);

            // Position - DO NOT transform for skinned meshes!
            // Vertices must remain in bind pose space; bone matrices handle transformation
            // Apply unit scale to convert to meters
            FbxVector4 position = controlPoints[controlPointIndex];
            vertex.position = glm::vec3(
                static_cast<float>(position[0]) * m_skeletalUnitScale,
                static_cast<float>(position[1]) * m_skeletalUnitScale,
                static_cast<float>(position[2]) * m_skeletalUnitScale
            );

            // UV
            if (mesh->GetElementUV(0)) {
                FbxVector2 uv;
                bool unmapped;
                mesh->GetPolygonVertexUV(polygonIndex, vertexIndex,
                                         mesh->GetElementUV(0)->GetName(), uv, unmapped);
                if (unmapped) {
                    vertex.texCoord = glm::vec2(0.5f, 0.5f);
                } else {
                    float u = static_cast<float>(uv[0]);
                    float v = static_cast<float>(uv[1]);
                    vertex.texCoord = glm::vec2(
                        glm::clamp(u, 0.0f, 1.0f),
                        glm::clamp(1.0f - v, 0.0f, 1.0f)
                    );
                }
            } else {
                vertex.texCoord = glm::vec2(0.5f, 0.5f);
            }

            // Normal - DO NOT transform for skinned meshes!
            // Normals must remain in bind pose space; bone matrices handle transformation
            if (mesh->GetElementNormal(0)) {
                FbxVector4 normal;
                mesh->GetPolygonVertexNormal(polygonIndex, vertexIndex, normal);

                // Keep normals as-is (winding reversal handles face orientation)
                vertex.normal = glm::normalize(glm::vec3(
                    static_cast<float>(normal[0]),
                    static_cast<float>(normal[1]),
                    static_cast<float>(normal[2])
                ));
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            // Color
            vertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
            vertex.tangent = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

            // Apply bone influences from control point
            const auto& influences = cpSkinData[controlPointIndex].boneInfluences;
            for (const auto& influence : influences) {
                vertex.addBoneInfluence(influence.first, influence.second);
            }
            vertex.normalizeWeights();

            meshData.vertices.push_back(vertex);
            meshData.indices.push_back(static_cast<uint32_t>(meshData.vertices.size() - 1));
        }
    }

    outData.meshes.push_back(std::move(meshData));
}

void ModelLoader::ExtractSkeleton(FbxScene* scene, SkeletalModelData& outData) {
    outData.skeleton = std::make_shared<MiEngine::Skeleton>();
    outData.hasSkeleton = false;

    FbxNode* rootNode = scene->GetRootNode();
    if (!rootNode) return;

    // First, collect all bone nodes and their cluster data from meshes
    // This is critical - we need the cluster matrices for correct bind poses
    std::unordered_map<std::string, std::pair<FbxAMatrix, FbxAMatrix>> boneClusterData; // boneName -> (meshTransform, boneTransform)

    std::function<void(FbxNode*)> collectClusterData = [&](FbxNode* node) {
        if (!node) return;

        FbxMesh* mesh = node->GetMesh();
        if (mesh) {
            int deformerCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
            for (int d = 0; d < deformerCount; ++d) {
                FbxSkin* skin = static_cast<FbxSkin*>(mesh->GetDeformer(d, FbxDeformer::eSkin));
                if (!skin) continue;

                int clusterCount = skin->GetClusterCount();
                for (int c = 0; c < clusterCount; ++c) {
                    FbxCluster* cluster = skin->GetCluster(c);
                    if (!cluster) continue;

                    FbxNode* boneNode = cluster->GetLink();
                    if (!boneNode) continue;

                    std::string boneName = boneNode->GetName();
                    if (boneClusterData.find(boneName) == boneClusterData.end()) {
                        FbxAMatrix meshTransform, boneTransform;
                        cluster->GetTransformMatrix(meshTransform);
                        cluster->GetTransformLinkMatrix(boneTransform);
                        boneClusterData[boneName] = {meshTransform, boneTransform};
                    }
                }
            }
        }

        for (int i = 0; i < node->GetChildCount(); ++i) {
            collectClusterData(node->GetChild(i));
        }
    };
    collectClusterData(rootNode);

    // Store cluster data for use in ProcessSkeletonNode
    m_boneClusterData = std::move(boneClusterData);

    // Look for skeleton nodes in the hierarchy
    std::function<void(FbxNode*, int)> findSkeletons = [&](FbxNode* node, int parentIndex) {
        if (!node) return;

        FbxNodeAttribute* attr = node->GetNodeAttribute();
        if (attr && attr->GetAttributeType() == FbxNodeAttribute::eSkeleton) {
            ProcessSkeletonNode(node, parentIndex, outData);
        } else {
            // Recursively search children
            for (int i = 0; i < node->GetChildCount(); ++i) {
                findSkeletons(node->GetChild(i), parentIndex);
            }
        }
    };

    for (int i = 0; i < rootNode->GetChildCount(); ++i) {
        findSkeletons(rootNode->GetChild(i), -1);
    }

    // If no skeleton found via skeleton nodes, try to extract from mesh deformers
    if (!outData.hasSkeleton) {
        std::function<void(FbxNode*)> findSkeletonFromMesh = [&](FbxNode* node) {
            if (!node) return;

            FbxMesh* mesh = node->GetMesh();
            if (mesh) {
                int deformerCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
                for (int d = 0; d < deformerCount; ++d) {
                    FbxSkin* skin = static_cast<FbxSkin*>(mesh->GetDeformer(d, FbxDeformer::eSkin));
                    if (!skin) continue;

                    int clusterCount = skin->GetClusterCount();
                    for (int c = 0; c < clusterCount; ++c) {
                        FbxCluster* cluster = skin->GetCluster(c);
                        if (!cluster) continue;

                        FbxNode* boneNode = cluster->GetLink();
                        if (boneNode && m_boneNameToIndex.find(boneNode->GetName()) == m_boneNameToIndex.end()) {
                            ProcessSkeletonNode(boneNode, -1, outData);
                        }
                    }
                }
            }

            for (int i = 0; i < node->GetChildCount(); ++i) {
                findSkeletonFromMesh(node->GetChild(i));
            }
        };

        findSkeletonFromMesh(rootNode);
    }
}

void ModelLoader::ProcessSkeletonNode(FbxNode* node, int parentIndex, SkeletalModelData& outData) {
    if (!node) return;

    std::string boneName = node->GetName();

    // Skip if already processed
    if (m_boneNameToIndex.find(boneName) != m_boneNameToIndex.end()) {
        return;
    }

    glm::mat4 inverseBindPose;
    glm::mat4 localBindPoseGlm;

    // Try to get bind pose from cluster data (this is the true bind pose used for skinning)
    auto clusterIt = m_boneClusterData.find(boneName);
    if (clusterIt != m_boneClusterData.end()) {
        // GetTransformLinkMatrix gives the bone's global transform at the moment of binding
        FbxAMatrix boneGlobalBindPose = clusterIt->second.second;

        // Scale the translation component for unit conversion
        FbxVector4 translation = boneGlobalBindPose.GetT();
        translation[0] *= m_skeletalUnitScale;
        translation[1] *= m_skeletalUnitScale;
        translation[2] *= m_skeletalUnitScale;
        boneGlobalBindPose.SetT(translation);

        // Use FBX's built-in inverse function, then convert
        FbxAMatrix invBindPoseFbx = boneGlobalBindPose.Inverse();
        inverseBindPose = FbxMatrixToGlm(invBindPoseFbx);
    } else {
        // Fallback: use node's global transform
        FbxAMatrix globalBindPose = node->EvaluateGlobalTransform();

        // Scale the translation component for unit conversion
        FbxVector4 translation = globalBindPose.GetT();
        translation[0] *= m_skeletalUnitScale;
        translation[1] *= m_skeletalUnitScale;
        translation[2] *= m_skeletalUnitScale;
        globalBindPose.SetT(translation);

        FbxAMatrix invBindPoseFbx = globalBindPose.Inverse();
        inverseBindPose = FbxMatrixToGlm(invBindPoseFbx);
    }

    // Get local bind pose for animation reference
    FbxAMatrix localBindPose = node->EvaluateLocalTransform();
    // Scale local bind pose translation as well
    FbxVector4 localT = localBindPose.GetT();
    localT[0] *= m_skeletalUnitScale;
    localT[1] *= m_skeletalUnitScale;
    localT[2] *= m_skeletalUnitScale;
    localBindPose.SetT(localT);
    localBindPoseGlm = FbxMatrixToGlm(localBindPose);

    // Add bone to skeleton
    uint32_t boneIndex = outData.skeleton->addBone(boneName, parentIndex, inverseBindPose, localBindPoseGlm);
    m_boneNameToIndex[boneName] = boneIndex;
    outData.hasSkeleton = true;

    // Process child bones
    for (int i = 0; i < node->GetChildCount(); ++i) {
        FbxNode* child = node->GetChild(i);
        FbxNodeAttribute* attr = child->GetNodeAttribute();
        if (attr && attr->GetAttributeType() == FbxNodeAttribute::eSkeleton) {
            ProcessSkeletonNode(child, static_cast<int>(boneIndex), outData);
        }
    }
}

void ModelLoader::ExtractSkinningData(FbxMesh* mesh, SkeletalMeshData& meshData,
                                       const SkeletalModelData& modelData) {
    // Implemented inline in ProcessSkeletalMesh
}

void ModelLoader::ExtractAnimations(FbxScene* scene, SkeletalModelData& outData) {
    if (!outData.skeleton || outData.skeleton->getBoneCount() == 0) {
        std::cout << "No skeleton, skipping animation extraction" << std::endl;
        return;
    }

    int animStackCount = scene->GetSrcObjectCount<FbxAnimStack>();

    for (int i = 0; i < animStackCount; ++i) {
        FbxAnimStack* animStack = scene->GetSrcObject<FbxAnimStack>(i);
        if (animStack) {
            ExtractAnimationStack(animStack, scene, outData);
        }
    }
}

void ModelLoader::ExtractAnimationStack(FbxAnimStack* animStack, FbxScene* scene,
                                         SkeletalModelData& outData) {
    std::string animName = animStack->GetName();

    // Set as current anim stack
    scene->SetCurrentAnimationStack(animStack);

    // Get time span
    FbxTimeSpan timeSpan = animStack->GetLocalTimeSpan();
    FbxTime startTime = timeSpan.GetStart();
    FbxTime endTime = timeSpan.GetStop();
    FbxTime duration = endTime - startTime;

    float durationSeconds = static_cast<float>(duration.GetSecondDouble());
    if (durationSeconds <= 0.0f) {
        return;
    }

    auto clip = std::make_shared<MiEngine::AnimationClip>(animName, durationSeconds, 30.0f);

    // Sample at 30 FPS
    FbxTime frameTime;
    frameTime.SetSecondDouble(1.0 / 30.0);

    // For each bone, extract animation keys
    for (uint32_t boneIdx = 0; boneIdx < outData.skeleton->getBoneCount(); ++boneIdx) {
        const MiEngine::Bone& bone = outData.skeleton->getBone(boneIdx);

        // Find the FBX node for this bone
        FbxNode* boneNode = scene->FindNodeByName(bone.name.c_str());
        if (!boneNode) continue;

        MiEngine::BoneAnimationTrack& track = clip->addTrack(bone.name);

        // Sample animation at each frame - store GLOBAL transform as matrix directly
        for (FbxTime time = startTime; time <= endTime; time += frameTime) {
            float keyTime = static_cast<float>((time - startTime).GetSecondDouble());

            // Get global transform at this time and store as matrix
            FbxAMatrix globalTransform = boneNode->EvaluateGlobalTransform(time);

            // Scale the translation component for unit conversion
            FbxVector4 translation = globalTransform.GetT();
            translation[0] *= m_skeletalUnitScale;
            translation[1] *= m_skeletalUnitScale;
            translation[2] *= m_skeletalUnitScale;
            globalTransform.SetT(translation);

            glm::mat4 globalMat = FbxMatrixToGlm(globalTransform);
            track.matrixKeys.push_back({keyTime, globalMat});
        }
    }

    // Mark this clip as using global transforms
    clip->setUsesGlobalTransforms(true);

    outData.animations.push_back(clip);
}

void ModelLoader::CalculateTangents(SkeletalMeshData& meshData) {
    if (meshData.vertices.empty() || meshData.indices.empty()) {
        return;
    }

    // Initialize tangents to zero
    for (auto& vertex : meshData.vertices) {
        vertex.tangent = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // Process triangles
    for (size_t i = 0; i < meshData.indices.size(); i += 3) {
        uint32_t i0 = meshData.indices[i];
        uint32_t i1 = meshData.indices[i + 1];
        uint32_t i2 = meshData.indices[i + 2];

        MiEngine::SkeletalVertex& v0 = meshData.vertices[i0];
        MiEngine::SkeletalVertex& v1 = meshData.vertices[i1];
        MiEngine::SkeletalVertex& v2 = meshData.vertices[i2];

        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;

        glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

        float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(denom) < 0.0001f) continue;

        float f = 1.0f / denom;

        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        v0.tangent.x += tangent.x;
        v0.tangent.y += tangent.y;
        v0.tangent.z += tangent.z;

        v1.tangent.x += tangent.x;
        v1.tangent.y += tangent.y;
        v1.tangent.z += tangent.z;

        v2.tangent.x += tangent.x;
        v2.tangent.y += tangent.y;
        v2.tangent.z += tangent.z;
    }

    // Normalize and orthogonalize
    for (auto& vertex : meshData.vertices) {
        glm::vec3 n = vertex.normal;
        glm::vec3 t = glm::vec3(vertex.tangent);

        if (glm::length(t) < 0.0001f) {
            // Generate arbitrary tangent
            if (std::abs(n.y) < 0.9f) {
                t = glm::normalize(glm::cross(n, glm::vec3(0, 1, 0)));
            } else {
                t = glm::normalize(glm::cross(n, glm::vec3(1, 0, 0)));
            }
        } else {
            t = glm::normalize(t - n * glm::dot(n, t));
        }

        glm::vec3 b = glm::cross(n, t);
        float handedness = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;

        vertex.tangent = glm::vec4(t, handedness);
    }
}
