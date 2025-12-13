#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "loader/ModelLoader.h"
#include "include/virtualgeo/VirtualGeoTypes.h"
#include "include/virtualgeo/MeshClusterer.h"
#include "include/virtualgeo/ClusterDAGBuilder.h"
#include "include/virtualgeo/ClusteredMeshCache.h"
#include "include/virtualgeo/VirtualGeoRenderer.h"
#include "include/debug/DebugUIManager.h"
#include "include/debug/VirtualGeoDebugPanel.h"
#include "VulkanRenderer.h"
#include <iostream>
#include <memory>
#include <filesystem>

class VirtualGeoTestGame : public Game {
public:
    void OnInit() override {
        std::cout << "=== Virtual Geometry Clustering Test ===" << std::endl;
        std::cout << "Comparing: Sphere, Flat Plane, and Robot2.fbx" << std::endl;

        // Setup lighting
        if (m_Scene) {
            m_Scene->clearLights();
            m_Scene->addLight(
                glm::vec3(1.0f, -1.0f, 0.5f),
                glm::vec3(1.0f, 0.95f, 0.9f),
                2.0f, 0.0f, 1.0f, true
            );
        }

        // Setup camera - wide view to see all three meshes
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(0.0f, 5.0f, 12.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            m_Camera->setFarPlane(10000.0f);  // Extended for LOD testing at distance
        }

        // Test clustering on all three mesh types
        TestAllMeshes();
    }

    void OnUpdate(float deltaTime) override {
        m_Time += deltaTime;
        CheckLODChange();
        // Note: VirtualGeoRenderer::beginFrame() is called by VulkanRenderer::drawFrame()
        // Do NOT call it here - double calls cause frame index to not alternate properly
    }

    void OnRender() override {}

    void OnShutdown() override {
        std::cout << "Virtual Geo Test Shutdown" << std::endl;
        m_ClusteredMeshes.clear();
    }

private:
    // Struct to hold each clustered mesh and its scene index
    struct ClusteredMeshInstance {
        std::unique_ptr<MiEngine::ClusteredMesh> mesh;
        MiEngine::ClusteringStats stats;
        size_t sceneIndex = 0;
        glm::vec3 position;
        std::string name;
        // GPU renderer IDs
        uint32_t gpuMeshId = 0;
        uint32_t gpuInstanceId = 0;
    };

    void TestAllMeshes() {
        if (!m_Scene || !m_Scene->getRenderer()) return;

        ModelLoader modelLoader;
        MiEngine::ClusteringOptions options;
        options.targetClusterSize = 128;
        options.minClusterSize = 64;
        options.simplificationRatio = 0.5f;
        options.maxLodLevels = 8;
        options.generateDebugColors = true;
        options.verbose = true;

        // ============================================================
        // 1. OCCLUDER WALL (regular PBR geometry - NOT VirtualGeo)
        // Rendered as regular geometry so it writes to depth buffer BEFORE
        // Hi-Z is built, enabling proper occlusion culling of VirtualGeo
        // ============================================================
        std::cout << "\n========================================" << std::endl;
        std::cout << "1. OCCLUDER WALL (PBR - not VirtualGeo)" << std::endl;
        std::cout << "========================================" << std::endl;
        {
            // Wall in XY plane at z=-5, blocking view of robot at z=-15 from camera at z=12
            // Wide in X (40), tall in Y (25), thin in Z (0.3)
            MeshData wallData = CreateWall(40.0f, 25.0f, 0.3f);  // width(X), height(Y), depth(Z)
            std::cout << "Wall: " << wallData.vertices.size() << " vertices, "
                      << wallData.indices.size() / 3 << " triangles" << std::endl;

            // Add wall as regular PBR geometry (not VirtualGeo)
            AddWallToScene(wallData, glm::vec3(0.0f, -0.5f, -5.0f));
        }

        // ============================================================
        // 2. ROBOT2.FBX (behind the wall)
        // ============================================================
        std::cout << "\n========================================" << std::endl;
        std::cout << "2. ROBOT2.FBX" << std::endl;
        std::cout << "========================================" << std::endl;
        {
            if (modelLoader.LoadModel("models/robot2.fbx")) {
                const auto& loadedMeshes = modelLoader.GetMeshData();
                if (!loadedMeshes.empty()) {
                    MeshData combinedData;
                    uint32_t vertexOffset = 0;
                    for (const auto& srcMesh : loadedMeshes) {
                        for (const auto& v : srcMesh.vertices) {
                            combinedData.vertices.push_back(v);
                        }
                        for (const auto& idx : srcMesh.indices) {
                            combinedData.indices.push_back(idx + vertexOffset);
                        }
                        vertexOffset += static_cast<uint32_t>(srcMesh.vertices.size());
                    }

                    std::cout << "Robot2: " << combinedData.vertices.size() << " vertices, "
                              << combinedData.indices.size() / 3 << " triangles" << std::endl;

                    std::vector<uint32_t> indices;
                    for (auto idx : combinedData.indices) {
                        indices.push_back(static_cast<uint32_t>(idx));
                    }

                    ClusteredMeshInstance instance;
                    instance.mesh = std::make_unique<MiEngine::ClusteredMesh>();
                    instance.mesh->name = "Robot2";
                    instance.name = "Robot2";
                    instance.position = glm::vec3(0.0f, 0.0f, -15.0f);  // Behind the wall (aligned with wall in X)

                    MiEngine::MeshClusterer clusterer;
                    if (clusterer.clusterMesh(combinedData.vertices, indices, options, *instance.mesh)) {
                        MiEngine::ClusterDAGBuilder dagBuilder;
                        dagBuilder.buildDAG(*instance.mesh, options);
                        instance.stats = clusterer.getStats();
                        instance.stats.lodLevels = instance.mesh->maxLodLevel + 1;
                        PrintMeshResults(*instance.mesh, "Robot2");
                        m_ClusteredMeshes.push_back(std::move(instance));
                    }
                }
            } else {
                std::cerr << "Failed to load robot2.fbx" << std::endl;
            }
        }

        // Skip adding to PBR scene - we'll use VirtualGeoRenderer instead
        // This avoids rendering meshes twice
        /*
        std::cout << "\n========================================" << std::endl;
        std::cout << "ADDING TO SCENE" << std::endl;
        std::cout << "========================================" << std::endl;

        for (auto& instance : m_ClusteredMeshes) {
            AddClusteredMeshToScene(instance);
        }
        */

        // Connect first mesh to debug panel
        if (!m_ClusteredMeshes.empty()) {
            UpdateDebugPanel(0);
        }

        // Print summary
        std::cout << "\n========================================" << std::endl;
        std::cout << "SUMMARY" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Layout: Wall (PBR occluder) | Robot2 (VirtualGeo behind wall)" << std::endl;
        std::cout << "Use debug panel to switch LOD levels" << std::endl;
        std::cout << "Hi-Z occlusion culling test: Robot should be culled when behind wall" << std::endl;

        // Test cache save/load
        TestCacheSaveLoad();

        // Upload meshes to VirtualGeoRenderer for GPU-driven rendering test
        UploadToVirtualGeoRenderer();
    }

    void TestCacheSaveLoad() {
        if (m_ClusteredMeshes.empty()) return;

        std::cout << "\n========================================" << std::endl;
        std::cout << "CACHE SAVE/LOAD TEST" << std::endl;
        std::cout << "========================================" << std::endl;

        namespace fs = std::filesystem;
        fs::path cacheDir = "Cache";
        fs::create_directories(cacheDir);

        // Test with the first mesh (Robot2)
        auto& instance = m_ClusteredMeshes[0];
        fs::path sourcePath = "test_robot.generated";
        fs::path cachePath = cacheDir / "test_robot.micluster";

        // Save
        std::cout << "\n--- Saving to cache ---" << std::endl;
        if (!MiEngine::ClusteredMeshCache::save(cachePath, *instance.mesh, sourcePath)) {
            std::cerr << "FAILED to save cache!" << std::endl;
            return;
        }

        // Print info
        std::cout << "\n--- Cache file info ---" << std::endl;
        MiEngine::ClusteredMeshCache::printInfo(cachePath);

        // Load into a new mesh
        std::cout << "\n--- Loading from cache ---" << std::endl;
        MiEngine::ClusteredMesh loadedMesh;
        if (!MiEngine::ClusteredMeshCache::load(cachePath, loadedMesh)) {
            std::cerr << "FAILED to load cache!" << std::endl;
            return;
        }

        // Verify data matches
        std::cout << "\n--- Verification ---" << std::endl;
        bool success = true;

        if (loadedMesh.clusters.size() != instance.mesh->clusters.size()) {
            std::cerr << "MISMATCH: Cluster count " << loadedMesh.clusters.size()
                      << " vs " << instance.mesh->clusters.size() << std::endl;
            success = false;
        }

        if (loadedMesh.vertices.size() != instance.mesh->vertices.size()) {
            std::cerr << "MISMATCH: Vertex count " << loadedMesh.vertices.size()
                      << " vs " << instance.mesh->vertices.size() << std::endl;
            success = false;
        }

        if (loadedMesh.indices.size() != instance.mesh->indices.size()) {
            std::cerr << "MISMATCH: Index count " << loadedMesh.indices.size()
                      << " vs " << instance.mesh->indices.size() << std::endl;
            success = false;
        }

        if (loadedMesh.maxLodLevel != instance.mesh->maxLodLevel) {
            std::cerr << "MISMATCH: Max LOD " << loadedMesh.maxLodLevel
                      << " vs " << instance.mesh->maxLodLevel << std::endl;
            success = false;
        }

        if (success) {
            std::cout << "SUCCESS: Cache round-trip verified!" << std::endl;
            std::cout << "  Clusters: " << loadedMesh.clusters.size() << std::endl;
            std::cout << "  Vertices: " << loadedMesh.vertices.size() << std::endl;
            std::cout << "  Indices: " << loadedMesh.indices.size() << std::endl;
            std::cout << "  LOD levels: " << loadedMesh.maxLodLevel + 1 << std::endl;
        }

        std::cout << "========================================" << std::endl;
    }

    // Create a wall (box) mesh for occlusion testing
    // width = X dimension, height = Y dimension, depth = Z dimension
    MeshData CreateWall(float width, float height, float depth) {
        MeshData meshData;
        float hw = width / 2.0f;
        float hh = height / 2.0f;
        float hd = depth / 2.0f;

        // Helper to add a quad with CLOCKWISE winding when viewed from outside
        // For VK_FRONT_FACE_CLOCKWISE, front faces have clockwise vertex order
        auto addQuad = [&](glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, glm::vec3 normal) {
            uint32_t base = static_cast<uint32_t>(meshData.vertices.size());

            Vertex verts[4];
            verts[0].position = v0; verts[0].texCoord = {0, 0};
            verts[1].position = v1; verts[1].texCoord = {1, 0};
            verts[2].position = v2; verts[2].texCoord = {1, 1};
            verts[3].position = v3; verts[3].texCoord = {0, 1};

            for (int i = 0; i < 4; i++) {
                verts[i].normal = normal;
                meshData.vertices.push_back(verts[i]);
            }

            // Two triangles - CLOCKWISE winding from the normal direction
            // First triangle: v0 -> v2 -> v1 (clockwise)
            meshData.indices.push_back(base + 0);
            meshData.indices.push_back(base + 2);
            meshData.indices.push_back(base + 1);
            // Second triangle: v0 -> v3 -> v2 (clockwise)
            meshData.indices.push_back(base + 0);
            meshData.indices.push_back(base + 3);
            meshData.indices.push_back(base + 2);
        };

        // Front face (+Z) - visible from +Z direction
        addQuad(
            {-hw, -hh, hd}, {hw, -hh, hd}, {hw, hh, hd}, {-hw, hh, hd},
            {0, 0, 1}
        );

        // Back face (-Z) - visible from -Z direction
        addQuad(
            {hw, -hh, -hd}, {-hw, -hh, -hd}, {-hw, hh, -hd}, {hw, hh, -hd},
            {0, 0, -1}
        );

        // Right face (+X) - visible from +X direction (toward robot)
        addQuad(
            {hw, -hh, hd}, {hw, -hh, -hd}, {hw, hh, -hd}, {hw, hh, hd},
            {1, 0, 0}
        );

        // Left face (-X) - visible from -X direction (toward camera)
        addQuad(
            {-hw, -hh, -hd}, {-hw, -hh, hd}, {-hw, hh, hd}, {-hw, hh, -hd},
            {-1, 0, 0}
        );

        // Top face (+Y)
        addQuad(
            {-hw, hh, hd}, {hw, hh, hd}, {hw, hh, -hd}, {-hw, hh, -hd},
            {0, 1, 0}
        );

        // Bottom face (-Y)
        addQuad(
            {-hw, -hh, -hd}, {hw, -hh, -hd}, {hw, -hh, hd}, {-hw, -hh, hd},
            {0, -1, 0}
        );

        return meshData;
    }

    MeshData CreateGridPlane(int gridSize, float planeSize) {
        MeshData meshData;
        float cellSize = planeSize / gridSize;

        // Generate vertices
        for (int z = 0; z <= gridSize; z++) {
            for (int x = 0; x <= gridSize; x++) {
                Vertex v;
                v.position = glm::vec3(
                    x * cellSize - planeSize / 2.0f,
                    0.0f,
                    z * cellSize - planeSize / 2.0f
                );
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                v.texCoord = glm::vec2(
                    static_cast<float>(x) / gridSize,
                    static_cast<float>(z) / gridSize
                );
                meshData.vertices.push_back(v);
            }
        }

        // Generate indices (clockwise winding for VK_FRONT_FACE_CLOCKWISE when viewed from above)
        for (int z = 0; z < gridSize; z++) {
            for (int x = 0; x < gridSize; x++) {
                uint32_t topLeft = z * (gridSize + 1) + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = topLeft + (gridSize + 1);
                uint32_t bottomRight = bottomLeft + 1;

                // Triangle 1: clockwise from above (topLeft -> topRight -> bottomLeft)
                meshData.indices.push_back(topLeft);
                meshData.indices.push_back(topRight);
                meshData.indices.push_back(bottomLeft);

                // Triangle 2: clockwise from above (topRight -> bottomRight -> bottomLeft)
                meshData.indices.push_back(topRight);
                meshData.indices.push_back(bottomRight);
                meshData.indices.push_back(bottomLeft);
            }
        }

        return meshData;
    }

    // Add wall as regular PBR geometry (not VirtualGeo) for proper occlusion
    void AddWallToScene(const MeshData& meshData, const glm::vec3& position) {
        if (!m_Scene || !m_Scene->getRenderer()) return;

        // Create a simple gray material for the wall
        auto material = std::make_shared<Material>();
        material->diffuseColor = glm::vec3(0.5f, 0.5f, 0.55f);  // Gray wall
        material->setPBRProperties(0.1f, 0.9f);  // Low metallic, high roughness

        VkDescriptorSet descSet = m_Scene->getRenderer()->createMaterialDescriptorSet(*material);
        if (descSet != VK_NULL_HANDLE) {
            material->setDescriptorSet(descSet);
        }

        auto mesh = std::make_shared<Mesh>(
            m_Scene->getRenderer()->getDevice(),
            m_Scene->getRenderer()->getPhysicalDevice(),
            meshData,
            material
        );
        mesh->createBuffers(
            m_Scene->getRenderer()->getCommandPool(),
            m_Scene->getRenderer()->getGraphicsQueue()
        );

        Transform transform;
        transform.position = position;
        transform.scale = glm::vec3(1.0f);

        m_Scene->addMeshInstance(mesh, transform);
        std::cout << "Added Wall as PBR geometry at position ("
                  << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
    }

    void PrintMeshResults(const MiEngine::ClusteredMesh& mesh, const std::string& name) {
        std::cout << "\n--- " << name << " Results ---" << std::endl;
        std::cout << "Total clusters: " << mesh.clusters.size() << std::endl;
        std::cout << "LOD levels: " << mesh.maxLodLevel + 1 << std::endl;

        for (uint32_t lod = 0; lod <= mesh.maxLodLevel; lod++) {
            uint32_t clusterCount = mesh.getClusterCountAtLod(lod);
            uint32_t triCount = mesh.getTriangleCountAtLod(lod);
            std::cout << "  LOD " << lod << ": " << clusterCount << " clusters, "
                      << triCount << " triangles" << std::endl;
        }
    }

    void AddClusteredMeshToScene(ClusteredMeshInstance& instance) {
        if (!m_Scene || !instance.mesh) return;

        MeshData meshData = BuildClusterColoredMesh(*instance.mesh, m_CurrentLOD);
        if (meshData.vertices.empty()) return;

        auto material = std::make_shared<Material>();
        material->diffuseColor = glm::vec3(1.0f, 1.0f, 1.0f);
        material->setPBRProperties(0.0f, 0.8f);

        VkDescriptorSet descSet = m_Scene->getRenderer()->createMaterialDescriptorSet(*material);
        if (descSet != VK_NULL_HANDLE) {
            material->setDescriptorSet(descSet);
        }

        auto mesh = std::make_shared<Mesh>(
            m_Scene->getRenderer()->getDevice(),
            m_Scene->getRenderer()->getPhysicalDevice(),
            meshData,
            material
        );
        mesh->createBuffers(
            m_Scene->getRenderer()->getCommandPool(),
            m_Scene->getRenderer()->getGraphicsQueue()
        );

        Transform transform;
        transform.position = instance.position;
        transform.scale = glm::vec3(1.0f);

        instance.sceneIndex = m_Scene->getMeshInstances().size();
        m_Scene->addMeshInstance(mesh, transform);

        std::cout << "Added " << instance.name << " at position ("
                  << instance.position.x << ", " << instance.position.y << ", " << instance.position.z
                  << ") - index " << instance.sceneIndex << std::endl;
    }

    MeshData BuildClusterColoredMesh(const MiEngine::ClusteredMesh& clusteredMesh, int lodLevel) {
        MeshData result;

        if (clusteredMesh.clusters.empty()) return result;

        uint32_t targetLOD = static_cast<uint32_t>(lodLevel);

        uint32_t currentVertexOffset = 0;
        for (const auto& cluster : clusteredMesh.clusters) {
            if (cluster.lodLevel != targetLOD) continue;

            glm::vec3 clusterColor(cluster.debugColor.r, cluster.debugColor.g, cluster.debugColor.b);

            for (uint32_t v = 0; v < cluster.vertexCount; v++) {
                uint32_t srcIdx = cluster.vertexOffset + v;
                if (srcIdx >= clusteredMesh.vertices.size()) continue;

                const auto& srcVertex = clusteredMesh.vertices[srcIdx];

                Vertex vertex;
                vertex.position = srcVertex.position;
                vertex.normal = srcVertex.normal;
                vertex.texCoord = srcVertex.texCoord;
                vertex.color = clusterColor;
                vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

                result.vertices.push_back(vertex);
            }

            for (uint32_t i = 0; i < cluster.triangleCount * 3; i++) {
                uint32_t srcIdx = cluster.indexOffset + i;
                if (srcIdx >= clusteredMesh.indices.size()) continue;

                uint32_t localIndex = clusteredMesh.indices[srcIdx];
                result.indices.push_back(currentVertexOffset + localIndex);
            }

            currentVertexOffset += cluster.vertexCount;
        }

        return result;
    }

    void UpdateDebugPanel(size_t meshIndex) {
        if (meshIndex >= m_ClusteredMeshes.size()) return;

        VulkanRenderer* renderer = m_Scene ? m_Scene->getRenderer() : m_Renderer;
        if (!renderer || !renderer->debugUI) return;

        auto vgeoPanel = renderer->debugUI->getPanel<MiEngine::VirtualGeoDebugPanel>("Virtual Geometry");
        if (vgeoPanel) {
            auto& instance = m_ClusteredMeshes[meshIndex];
            vgeoPanel->setClusteredMesh(instance.mesh.get());
            vgeoPanel->setClusteringStats(instance.stats);
            vgeoPanel->setOpen(true);
            m_SelectedMeshIndex = meshIndex;
            std::cout << "Debug panel showing: " << instance.name << std::endl;
        }
    }

    void CheckLODChange() {
        if (!m_Scene || !m_Scene->getRenderer()) return;

        VulkanRenderer* renderer = m_Scene->getRenderer();
        if (!renderer || !renderer->debugUI) return;

        auto vgeoPanel = renderer->debugUI->getPanel<MiEngine::VirtualGeoDebugPanel>("Virtual Geometry");
        if (!vgeoPanel) return;

        int selectedLOD = vgeoPanel->getSelectedLOD();
        int displayLOD = (selectedLOD < 0) ? 0 : selectedLOD;

        // Rebuild all meshes if LOD changed
        if (displayLOD != m_CurrentLOD) {
            std::cout << "LOD changed from " << m_CurrentLOD << " to " << displayLOD << std::endl;
            m_CurrentLOD = displayLOD;

            for (auto& instance : m_ClusteredMeshes) {
                RebuildMeshForLOD(instance, displayLOD);
            }
        }
    }

    void RebuildMeshForLOD(ClusteredMeshInstance& instance, int lodLevel) {
        if (!m_Scene || !instance.mesh) return;

        // Clamp LOD to valid range for this mesh
        int maxLod = static_cast<int>(instance.mesh->maxLodLevel);
        int clampedLod = std::min(lodLevel, maxLod);

        MeshData meshData = BuildClusterColoredMesh(*instance.mesh, clampedLod);
        if (meshData.vertices.empty()) return;

        auto* meshInstance = m_Scene->getMeshInstance(instance.sceneIndex);
        if (meshInstance && meshInstance->mesh) {
            auto material = meshInstance->mesh->getMaterial();

            auto newMesh = std::make_shared<Mesh>(
                m_Scene->getRenderer()->getDevice(),
                m_Scene->getRenderer()->getPhysicalDevice(),
                meshData,
                material
            );
            newMesh->createBuffers(
                m_Scene->getRenderer()->getCommandPool(),
                m_Scene->getRenderer()->getGraphicsQueue()
            );

            meshInstance->mesh = newMesh;
        }
    }

    void UploadToVirtualGeoRenderer() {
        VulkanRenderer* renderer = m_Scene ? m_Scene->getRenderer() : m_Renderer;
        if (!renderer) return;

        MiEngine::VirtualGeoRenderer* vgRenderer = renderer->getVirtualGeoRenderer();
        if (!vgRenderer || !vgRenderer->isInitialized()) {
            std::cout << "\n[VirtualGeoRenderer] Not initialized, skipping GPU upload" << std::endl;
            return;
        }

        std::cout << "\n========================================" << std::endl;
        std::cout << "UPLOADING TO VIRTUALGEORENDERER" << std::endl;
        std::cout << "========================================" << std::endl;

        // Upload each clustered mesh to the GPU renderer
        for (size_t i = 0; i < m_ClusteredMeshes.size(); i++) {
            auto& instance = m_ClusteredMeshes[i];
            if (!instance.mesh) continue;

            uint32_t meshId = vgRenderer->uploadClusteredMesh(*instance.mesh);
            if (meshId > 0) {
                instance.gpuMeshId = meshId;

                // Create transform matrix
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), instance.position);

                // Add instance
                uint32_t instanceId = vgRenderer->addInstance(meshId, transform);
                instance.gpuInstanceId = instanceId;

                std::cout << "Uploaded " << instance.name << ": meshId=" << meshId
                          << ", instanceId=" << instanceId
                          << ", clusters=" << instance.mesh->clusters.size() << std::endl;
            } else {
                std::cout << "Failed to upload " << instance.name << std::endl;
            }
        }

        // Update debug panel with VirtualGeoRenderer reference
        if (renderer->debugUI) {
            auto vgeoPanel = renderer->debugUI->getPanel<MiEngine::VirtualGeoDebugPanel>("Virtual Geometry");
            if (vgeoPanel) {
                vgeoPanel->setVirtualGeoRenderer(vgRenderer);
                std::cout << "\nVirtualGeoRenderer connected to debug panel" << std::endl;
            }
        }

        // Print statistics
        std::cout << "\nGPU Renderer Statistics:" << std::endl;
        std::cout << "  Meshes: " << vgRenderer->getMeshCount() << std::endl;
        std::cout << "  Instances: " << vgRenderer->getInstanceCount() << std::endl;
        std::cout << "  Total Clusters: " << vgRenderer->getTotalClusterCount() << std::endl;
        std::cout << "========================================" << std::endl;
    }

    std::vector<ClusteredMeshInstance> m_ClusteredMeshes;
    float m_Time = 0.0f;
    int m_CurrentLOD = 0;
    size_t m_SelectedMeshIndex = 0;
};
