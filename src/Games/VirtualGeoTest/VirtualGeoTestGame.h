#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "loader/ModelLoader.h"
#include "include/virtualgeo/VirtualGeoTypes.h"
#include "include/virtualgeo/MeshClusterer.h"
#include "include/virtualgeo/ClusterDAGBuilder.h"
#include "include/debug/DebugUIManager.h"
#include "include/debug/VirtualGeoDebugPanel.h"
#include "VulkanRenderer.h"
#include <iostream>
#include <memory>

class VirtualGeoTestGame : public Game {
public:
    void OnInit() override {
        std::cout << "=== Virtual Geometry Clustering Test ===" << std::endl;

        // Setup lighting
        if (m_Scene) {
            m_Scene->clearLights();
            m_Scene->addLight(
                glm::vec3(1.0f, -1.0f, 0.5f),
                glm::vec3(1.0f, 0.95f, 0.9f),
                2.0f, 0.0f, 1.0f, true
            );
        }

        // Setup camera
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(0.0f, 2.0f, 5.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            m_Camera->setFarPlane(100.0f);
        }

        // Test clustering on a high-poly mesh
        TestClustering();
    }

    void OnUpdate(float deltaTime) override {
        m_Time += deltaTime;

        // Cycle through LOD levels with number keys for visualization
        // This would be connected to debug UI in a full implementation
    }

    void OnRender() override {
        // Render clustered mesh with debug colors
        // In a full implementation, this would render using the Virtual Geo pipeline
    }

    void OnShutdown() override {
        std::cout << "Virtual Geo Test Shutdown" << std::endl;
        m_ClusteredMesh.reset();
    }

private:
    void TestClustering() {
        if (!m_Scene || !m_Scene->getRenderer()) return;

        ModelLoader modelLoader;

        // Create a high-poly sphere for testing (32 segments = ~2000 triangles)
        std::cout << "\n--- Creating test mesh ---" << std::endl;
        MeshData sphereData = modelLoader.CreateSphere(1.0f, 32, 32);
        std::cout << "Sphere: " << sphereData.vertices.size() << " vertices, "
                  << sphereData.indices.size() / 3 << " triangles" << std::endl;

        // Convert indices to uint32_t
        std::vector<uint32_t> indices;
        indices.reserve(sphereData.indices.size());
        for (auto idx : sphereData.indices) {
            indices.push_back(static_cast<uint32_t>(idx));
        }

        // Run clustering
        std::cout << "\n--- Running mesh clustering ---" << std::endl;
        MiEngine::MeshClusterer clusterer;
        MiEngine::ClusteringOptions options;
        options.targetClusterSize = 128;
        options.minClusterSize = 64;
        options.simplificationRatio = 0.5f;
        options.maxLodLevels = 8;
        options.generateDebugColors = true;
        options.verbose = true;

        m_ClusteredMesh = std::make_unique<MiEngine::ClusteredMesh>();
        m_ClusteredMesh->name = "TestSphere";

        if (!clusterer.clusterMesh(sphereData.vertices, indices, options, *m_ClusteredMesh)) {
            std::cerr << "Clustering failed!" << std::endl;
            return;
        }

        m_ClusteringStats = clusterer.getStats();

        // Build LOD hierarchy
        std::cout << "\n--- Building LOD hierarchy ---" << std::endl;
        MiEngine::ClusterDAGBuilder dagBuilder;
        if (!dagBuilder.buildDAG(*m_ClusteredMesh, options)) {
            std::cerr << "DAG building failed!" << std::endl;
            return;
        }

        m_ClusteringStats.dagBuildTime = 0;  // Would get from timer
        m_ClusteringStats.lodLevels = m_ClusteredMesh->maxLodLevel + 1;

        // Connect to debug panel
        UpdateDebugPanel();

        // Print results
        PrintClusteringResults();

        // Add visualization mesh to scene
        AddClusteredMeshToScene();
    }

    void PrintClusteringResults() {
        std::cout << "\n=== Clustering Results ===" << std::endl;
        std::cout << "Mesh: " << m_ClusteredMesh->name << std::endl;
        std::cout << "Total clusters: " << m_ClusteredMesh->clusters.size() << std::endl;
        std::cout << "LOD levels: " << m_ClusteredMesh->maxLodLevel + 1 << std::endl;
        std::cout << "Leaf clusters (LOD 0): " << m_ClusteredMesh->leafClusterCount << std::endl;
        std::cout << "Root clusters (LOD " << m_ClusteredMesh->maxLodLevel << "): "
                  << m_ClusteredMesh->rootClusterCount << std::endl;

        std::cout << "\nPer-LOD breakdown:" << std::endl;
        for (uint32_t lod = 0; lod <= m_ClusteredMesh->maxLodLevel; lod++) {
            uint32_t clusterCount = m_ClusteredMesh->getClusterCountAtLod(lod);
            uint32_t triCount = m_ClusteredMesh->getTriangleCountAtLod(lod);
            std::cout << "  LOD " << lod << ": " << clusterCount << " clusters, "
                      << triCount << " triangles" << std::endl;
        }

        std::cout << "\nBounding volume:" << std::endl;
        std::cout << "  Center: (" << m_ClusteredMesh->boundingSphereCenter.x << ", "
                  << m_ClusteredMesh->boundingSphereCenter.y << ", "
                  << m_ClusteredMesh->boundingSphereCenter.z << ")" << std::endl;
        std::cout << "  Radius: " << m_ClusteredMesh->boundingSphereRadius << std::endl;

        std::cout << "\nDebug colors assigned to " << m_ClusteredMesh->clusters.size() << " clusters" << std::endl;

        // Print first few clusters with their debug colors
        std::cout << "\nFirst 5 cluster debug colors:" << std::endl;
        for (size_t i = 0; i < std::min(size_t(5), m_ClusteredMesh->clusters.size()); i++) {
            const auto& c = m_ClusteredMesh->clusters[i];
            std::cout << "  Cluster " << c.clusterId << " (LOD " << c.lodLevel << "): RGB("
                      << c.debugColor.r << ", " << c.debugColor.g << ", " << c.debugColor.b << ")"
                      << " - " << c.triangleCount << " tris" << std::endl;
        }

        std::cout << "===========================" << std::endl;
    }

    void UpdateDebugPanel() {
        std::cout << "\n--- Connecting to Debug Panel ---" << std::endl;

        // Get renderer from Scene (more reliable than m_Renderer in some cases)
        VulkanRenderer* renderer = m_Scene ? m_Scene->getRenderer() : m_Renderer;

        std::cout << "  Scene: " << (m_Scene ? "valid" : "null") << std::endl;
        std::cout << "  Renderer: " << (renderer ? "valid" : "null") << std::endl;
        std::cout << "  DebugUI: " << (renderer && renderer->debugUI ? "valid" : "null") << std::endl;
        std::cout << "  ClusteredMesh: " << (m_ClusteredMesh ? "valid" : "null") << std::endl;

        if (!renderer || !renderer->debugUI || !m_ClusteredMesh) {
            std::cout << "  ERROR: Missing required component!" << std::endl;
            return;
        }

        auto vgeoPanel = renderer->debugUI->getPanel<MiEngine::VirtualGeoDebugPanel>("Virtual Geometry");
        std::cout << "  VirtualGeoPanel: " << (vgeoPanel ? "found" : "NOT FOUND") << std::endl;

        if (vgeoPanel) {
            vgeoPanel->setClusteredMesh(m_ClusteredMesh.get());
            vgeoPanel->setClusteringStats(m_ClusteringStats);
            vgeoPanel->setOpen(true);  // Auto-open the panel
            std::cout << "  SUCCESS: Connected " << m_ClusteredMesh->clusters.size() << " clusters to panel" << std::endl;
        } else {
            std::cout << "  ERROR: Virtual Geometry panel not found in debugUI!" << std::endl;
        }
        std::cout << "-----------------------------------" << std::endl;
    }

    void AddClusteredMeshToScene() {
        if (!m_Scene || !m_ClusteredMesh) return;

        // Build mesh from clustered data with per-vertex cluster colors
        MeshData meshData = BuildClusterColoredMesh();

        if (meshData.vertices.empty()) {
            std::cerr << "Failed to build cluster-colored mesh!" << std::endl;
            return;
        }

        // Create material with white base color (vertex colors will show through)
        auto material = std::make_shared<Material>();
        material->diffuseColor = glm::vec3(1.0f, 1.0f, 1.0f);  // White so vertex colors show
        material->setPBRProperties(0.0f, 0.8f);  // Non-metallic, slightly rough

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
        transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
        transform.scale = glm::vec3(1.0f);

        m_Scene->addMeshInstance(mesh, transform);

        std::cout << "\nAdded cluster-colored mesh to scene" << std::endl;
        std::cout << "  Vertices: " << meshData.vertices.size() << std::endl;
        std::cout << "  Triangles: " << meshData.indices.size() / 3 << std::endl;
        std::cout << "  Each cluster has a unique color for visualization" << std::endl;
    }

    MeshData BuildClusterColoredMesh() {
        MeshData result;

        if (!m_ClusteredMesh || m_ClusteredMesh->clusters.empty()) {
            return result;
        }

        // Reserve space
        size_t totalVertices = 0;
        size_t totalIndices = 0;
        for (const auto& cluster : m_ClusteredMesh->clusters) {
            // Only use LOD 0 (highest detail) clusters for visualization
            if (cluster.lodLevel == 0) {
                totalVertices += cluster.vertexCount;
                totalIndices += cluster.triangleCount * 3;
            }
        }

        result.vertices.reserve(totalVertices);
        result.indices.reserve(totalIndices);

        uint32_t currentVertexOffset = 0;

        // Process only LOD 0 clusters
        for (const auto& cluster : m_ClusteredMesh->clusters) {
            if (cluster.lodLevel != 0) continue;

            glm::vec3 clusterColor(cluster.debugColor.r, cluster.debugColor.g, cluster.debugColor.b);

            // Copy vertices with cluster color
            for (uint32_t v = 0; v < cluster.vertexCount; v++) {
                uint32_t srcIdx = cluster.vertexOffset + v;
                if (srcIdx >= m_ClusteredMesh->vertices.size()) continue;

                const auto& srcVertex = m_ClusteredMesh->vertices[srcIdx];

                Vertex vertex;
                vertex.position = srcVertex.position;
                vertex.normal = srcVertex.normal;
                vertex.texCoord = srcVertex.texCoord;
                vertex.color = clusterColor;  // Set cluster debug color!
                vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);  // Default tangent

                result.vertices.push_back(vertex);
            }

            // Copy indices with offset adjustment
            // The indices in ClusteredMesh are already LOCAL (0 to vertexCount-1) for each cluster
            for (uint32_t i = 0; i < cluster.triangleCount * 3; i++) {
                uint32_t srcIdx = cluster.indexOffset + i;
                if (srcIdx >= m_ClusteredMesh->indices.size()) continue;

                // The cluster indices are already local (0 to vertexCount-1)
                // Just add our current offset in the output buffer
                uint32_t localIndex = m_ClusteredMesh->indices[srcIdx];
                result.indices.push_back(currentVertexOffset + localIndex);
            }

            currentVertexOffset += cluster.vertexCount;
        }

        std::cout << "Built cluster-colored mesh:" << std::endl;
        std::cout << "  LOD 0 clusters: " << m_ClusteredMesh->getClusterCountAtLod(0) << std::endl;
        std::cout << "  Vertices: " << result.vertices.size() << std::endl;
        std::cout << "  Indices: " << result.indices.size() << std::endl;

        // Debug: Print first few vertex colors to verify they are set
        std::cout << "  Sample vertex colors:" << std::endl;
        for (size_t i = 0; i < std::min(size_t(5), result.vertices.size()); i++) {
            const auto& v = result.vertices[i];
            std::cout << "    Vertex " << i << ": color=("
                      << v.color.r << ", " << v.color.g << ", " << v.color.b << ")" << std::endl;
        }

        // Debug: Check for out-of-bounds indices
        uint32_t maxIndex = 0;
        uint32_t outOfBoundsCount = 0;
        for (size_t i = 0; i < result.indices.size(); i++) {
            if (result.indices[i] >= result.vertices.size()) {
                outOfBoundsCount++;
            }
            maxIndex = std::max(maxIndex, result.indices[i]);
        }
        std::cout << "  Max index: " << maxIndex << " (vertex count: " << result.vertices.size() << ")" << std::endl;
        if (outOfBoundsCount > 0) {
            std::cout << "  WARNING: " << outOfBoundsCount << " out-of-bounds indices!" << std::endl;
        }

        // Debug: Print first cluster info
        int clusterIdx = 0;
        for (const auto& cluster : m_ClusteredMesh->clusters) {
            if (cluster.lodLevel == 0 && clusterIdx < 3) {
                std::cout << "  Cluster " << cluster.clusterId << ": vertexOffset=" << cluster.vertexOffset
                          << ", vertexCount=" << cluster.vertexCount
                          << ", indexOffset=" << cluster.indexOffset
                          << ", triCount=" << cluster.triangleCount << std::endl;
                // Print first few indices for this cluster
                std::cout << "    First indices: ";
                for (uint32_t i = 0; i < std::min(6u, cluster.triangleCount * 3); i++) {
                    std::cout << m_ClusteredMesh->indices[cluster.indexOffset + i] << " ";
                }
                std::cout << std::endl;
                clusterIdx++;
            }
        }

        return result;
    }

    std::unique_ptr<MiEngine::ClusteredMesh> m_ClusteredMesh;
    MiEngine::ClusteringStats m_ClusteringStats{};
    float m_Time = 0.0f;
    int m_CurrentLOD = 0;
};
