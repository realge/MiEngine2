#include "include/debug/VirtualGeoDebugPanel.h"
#include "include/virtualgeo/VirtualGeoTypes.h"
#include "imgui.h"

namespace MiEngine {

VirtualGeoDebugPanel::VirtualGeoDebugPanel()
    : DebugPanel("Virtual Geometry", nullptr) {}

void VirtualGeoDebugPanel::draw() {
    ImGui::Begin("Virtual Geometry Debug", &isOpen, ImGuiWindowFlags_AlwaysAutoResize);

    if (!m_ClusteredMesh) {
        ImGui::Text("No clustered mesh loaded");
        ImGui::End();
        return;
    }

    renderStats();
    ImGui::Separator();
    renderClusterInfo();
    ImGui::Separator();
    renderLODSelector();
    ImGui::Separator();
    renderVisualizationOptions();

    ImGui::End();
}

void VirtualGeoDebugPanel::renderStats() {
    ImGui::Text("Clustering Statistics");
    ImGui::Indent();
    ImGui::Text("Input: %u triangles, %u vertices", m_Stats.inputTriangles, m_Stats.inputVertices);
    ImGui::Text("Output: %u clusters, %u LOD levels", m_Stats.outputClusters, m_Stats.lodLevels);
    ImGui::Text("Avg cluster size: %.1f triangles", m_Stats.averageClusterSize);
    ImGui::Text("Clustering time: %.2f ms", m_Stats.clusteringTime);
    ImGui::Text("DAG build time: %.2f ms", m_Stats.dagBuildTime);
    ImGui::Text("Total time: %.2f ms", m_Stats.totalTime);
    ImGui::Unindent();
}

void VirtualGeoDebugPanel::renderClusterInfo() {
    ImGui::Text("Mesh: %s", m_ClusteredMesh->name.c_str());
    ImGui::Text("Total clusters: %zu", m_ClusteredMesh->clusters.size());
    ImGui::Text("Total vertices: %u", m_ClusteredMesh->totalVertices);
    ImGui::Text("Total triangles: %u", m_ClusteredMesh->totalTriangles);
    ImGui::Text("LOD levels: %u", m_ClusteredMesh->maxLodLevel + 1);

    // Cluster counts per LOD
    if (ImGui::TreeNode("Clusters per LOD")) {
        for (uint32_t lod = 0; lod <= m_ClusteredMesh->maxLodLevel; lod++) {
            uint32_t count = m_ClusteredMesh->getClusterCountAtLod(lod);
            uint32_t tris = m_ClusteredMesh->getTriangleCountAtLod(lod);
            ImGui::Text("LOD %u: %u clusters, %u triangles", lod, count, tris);
        }
        ImGui::TreePop();
    }

    // Bounding volume info
    if (ImGui::TreeNode("Bounding Volume")) {
        ImGui::Text("Sphere center: (%.2f, %.2f, %.2f)",
            m_ClusteredMesh->boundingSphereCenter.x,
            m_ClusteredMesh->boundingSphereCenter.y,
            m_ClusteredMesh->boundingSphereCenter.z);
        ImGui::Text("Sphere radius: %.2f", m_ClusteredMesh->boundingSphereRadius);
        ImGui::Text("AABB min: (%.2f, %.2f, %.2f)",
            m_ClusteredMesh->aabbMin.x,
            m_ClusteredMesh->aabbMin.y,
            m_ClusteredMesh->aabbMin.z);
        ImGui::Text("AABB max: (%.2f, %.2f, %.2f)",
            m_ClusteredMesh->aabbMax.x,
            m_ClusteredMesh->aabbMax.y,
            m_ClusteredMesh->aabbMax.z);
        ImGui::TreePop();
    }
}

void VirtualGeoDebugPanel::renderLODSelector() {
    ImGui::Text("LOD Selection");

    bool autoLOD = (m_SelectedLOD < 0);
    if (ImGui::Checkbox("Auto LOD", &autoLOD)) {
        m_SelectedLOD = autoLOD ? -1 : 0;
    }

    if (!autoLOD) {
        int maxLOD = static_cast<int>(m_ClusteredMesh->maxLodLevel);
        ImGui::SliderInt("Force LOD", &m_SelectedLOD, 0, maxLOD);
    }

    ImGui::SliderFloat("Error Threshold", &m_LODErrorThreshold, 0.1f, 10.0f, "%.1f px");
}

void VirtualGeoDebugPanel::renderVisualizationOptions() {
    ImGui::Text("Visualization");

    ImGui::Checkbox("Show Cluster Colors", &m_ShowClusterColors);
    ImGui::Checkbox("Show LOD Colors", &m_ShowLODColors);
    ImGui::Checkbox("Wireframe", &m_ShowWireframe);
    ImGui::Checkbox("Bounding Spheres", &m_ShowBoundingSpheres);

    if (m_ShowLODColors) {
        // Legend for LOD colors
        ImGui::Text("LOD Legend:");
        ImVec4 lodColors[] = {
            {1.0f, 0.0f, 0.0f, 1.0f},  // LOD 0 - Red
            {1.0f, 0.5f, 0.0f, 1.0f},  // LOD 1 - Orange
            {1.0f, 1.0f, 0.0f, 1.0f},  // LOD 2 - Yellow
            {0.0f, 1.0f, 0.0f, 1.0f},  // LOD 3 - Green
            {0.0f, 1.0f, 1.0f, 1.0f},  // LOD 4 - Cyan
            {0.0f, 0.0f, 1.0f, 1.0f},  // LOD 5 - Blue
        };
        for (uint32_t i = 0; i <= std::min(m_ClusteredMesh->maxLodLevel, 5u); i++) {
            ImGui::ColorButton(("##lod" + std::to_string(i)).c_str(), lodColors[i], ImGuiColorEditFlags_NoTooltip);
            ImGui::SameLine();
            ImGui::Text("LOD %u", i);
        }
    }
}

} // namespace MiEngine
