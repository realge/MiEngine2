#include "include/debug/VirtualGeoDebugPanel.h"
#include "include/virtualgeo/VirtualGeoTypes.h"
#include "include/virtualgeo/VirtualGeoRenderer.h"
#include "imgui.h"

namespace MiEngine {

VirtualGeoDebugPanel::VirtualGeoDebugPanel(VulkanRenderer* renderer)
    : DebugPanel("Virtual Geometry", renderer) {}

void VirtualGeoDebugPanel::draw() {
    ImGui::Begin("Virtual Geometry Debug", &isOpen, ImGuiWindowFlags_AlwaysAutoResize);

    // Show renderer controls if available
    if (m_VGRenderer) {
        renderRendererControls();
        ImGui::Separator();
    }

    if (!m_ClusteredMesh && !m_VGRenderer) {
        ImGui::Text("No clustered mesh loaded");
        ImGui::Text("Use Asset Browser to generate clustered meshes");
        ImGui::End();
        return;
    }

    if (m_ClusteredMesh) {
        renderStats();
        ImGui::Separator();
        renderClusterInfo();
        ImGui::Separator();
        renderLODSelector();
        ImGui::Separator();
        renderVisualizationOptions();
    }

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

    ImGui::SliderFloat("Error Threshold##LocalLOD", &m_LODErrorThreshold, 0.1f, 10.0f, "%.1f px");
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

void VirtualGeoDebugPanel::renderRendererControls() {
    ImGui::Text("Virtual Geometry Renderer");

    if (!m_VGRenderer->isInitialized()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Renderer not initialized");
        return;
    }

    // Status indicator
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[Status: GPU rendering active]");
    ImGui::Spacing();

    // Statistics
    ImGui::Text("Statistics:");
    ImGui::Indent();
    ImGui::Text("Meshes: %u", m_VGRenderer->getMeshCount());
    ImGui::Text("Instances: %u", m_VGRenderer->getInstanceCount());
    ImGui::Text("Total Clusters: %u", m_VGRenderer->getTotalClusterCount());
    ImGui::Text("Visible Clusters: %u", m_VGRenderer->getVisibleClusterCount());
    ImGui::Text("Draw Calls: %u", m_VGRenderer->getDrawCallCount());
    ImGui::Text("Max LOD Level: %u", m_VGRenderer->getMaxLodLevel());

    // Culling efficiency
    uint32_t total = m_VGRenderer->getTotalClusterCount();
    uint32_t visible = m_VGRenderer->getVisibleClusterCount();
    if (total > 0) {
        float cullRate = 100.0f * (1.0f - static_cast<float>(visible) / static_cast<float>(total));
        ImGui::Text("Cull Rate: %.1f%%", cullRate);
    }
    ImGui::Unindent();

    ImGui::Spacing();

    // LOD Control
    ImGui::Text("LOD Control:");
    ImGui::Indent();

    uint32_t maxLod = m_VGRenderer->getMaxLodLevel();
    int forcedLod = static_cast<int>(m_VGRenderer->getForcedLodLevel());
    if (ImGui::SliderInt("Force LOD Level", &forcedLod, 0, static_cast<int>(maxLod))) {
        m_VGRenderer->setForcedLodLevel(static_cast<uint32_t>(forcedLod));
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##ForcedLOD")) {
        m_VGRenderer->setForcedLodLevel(0);
    }

    // LOD level legend
    ImGui::Text("LOD %d: Highest detail", 0);
    if (maxLod > 0) {
        ImGui::Text("LOD %u: Lowest detail", maxLod);
    }

    ImGui::Unindent();

    ImGui::Spacing();

    // Rendering Mode
    ImGui::Text("Rendering Mode:");
    ImGui::Indent();

    bool gpuDriven = m_VGRenderer->isGPUDrivenEnabled();
    if (ImGui::Checkbox("GPU-Driven Rendering", &gpuDriven)) {
        m_VGRenderer->setGPUDrivenEnabled(gpuDriven);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable compute-based culling and indirect draw.\nRequires merged buffers to be built.");
    }

    if (gpuDriven) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Mode: GPU-Driven (Indirect Draw)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Mode: Direct Draw (Manual LOD)");
    }

    ImGui::Unindent();

    ImGui::Spacing();

    // Culling settings
    ImGui::Text("Culling Settings:");
    ImGui::Indent();

    bool frustumCulling = m_VGRenderer->isFrustumCullingEnabled();
    if (ImGui::Checkbox("Frustum Culling", &frustumCulling)) {
        m_VGRenderer->setFrustumCullingEnabled(frustumCulling);
    }

    bool occlusionCulling = m_VGRenderer->isOcclusionCullingEnabled();
    if (ImGui::Checkbox("Hi-Z Occlusion Culling", &occlusionCulling)) {
        m_VGRenderer->setOcclusionCullingEnabled(occlusionCulling);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Cull clusters occluded by closer geometry using hierarchical-Z buffer");
    }

    // Hi-Z occlusion parameters (only show when occlusion culling is enabled)
    if (occlusionCulling) {
        ImGui::Indent();

        float hizMaxMip = m_VGRenderer->getHiZMaxMipLevel();
        if (ImGui::SliderFloat("Max Mip Level", &hizMaxMip, 0.0f, 8.0f, "%.1f")) {
            m_VGRenderer->setHiZMaxMipLevel(hizMaxMip);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Maximum Hi-Z mip level to sample.\nLower = more accurate but slower.\nHigher = faster but may miss small occluders.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset##HiZMip")) {
            m_VGRenderer->setHiZMaxMipLevel(3.0f);
        }

        float hizBias = m_VGRenderer->getHiZDepthBias();
        if (ImGui::SliderFloat("Depth Bias", &hizBias, 0.0f, 0.1f, "%.4f")) {
            m_VGRenderer->setHiZDepthBias(hizBias);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Bias added to Hi-Z depth comparison.\nHigher = more conservative (less false occlusion).\nLower = more aggressive culling.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset##HiZBias")) {
            m_VGRenderer->setHiZDepthBias(0.02f);
        }

        float hizThreshold = m_VGRenderer->getHiZDepthThreshold();
        if (ImGui::SliderFloat("Far Threshold", &hizThreshold, 0.9f, 1.0f, "%.4f")) {
            m_VGRenderer->setHiZDepthThreshold(hizThreshold);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Depth threshold for detecting 'no occluder'.\nIf Hi-Z depth > threshold, skip occlusion test.\nPrevents culling against empty space.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset##HiZThreshold")) {
            m_VGRenderer->setHiZDepthThreshold(0.999f);
        }

        ImGui::Spacing();

        // Hi-Z Debug Visualization
        bool hizDebug = m_VGRenderer->isHiZDebugEnabled();
        if (ImGui::Checkbox("Show Hi-Z Buffer", &hizDebug)) {
            m_VGRenderer->setHiZDebugEnabled(hizDebug);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Visualize the Hi-Z depth pyramid.\nWhite/Red = near, Black/Blue = far.");
        }

        if (hizDebug) {
            float debugMip = m_VGRenderer->getHiZDebugMipLevel();
            float maxMip = static_cast<float>(m_VGRenderer->getHiZMipLevels() - 1);
            if (ImGui::SliderFloat("View Mip Level", &debugMip, 0.0f, maxMip, "%.0f")) {
                m_VGRenderer->setHiZDebugMipLevel(debugMip);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Which mip level of the Hi-Z pyramid to display.\n0 = full resolution, higher = coarser.");
            }

            const char* modeNames[] = { "Grayscale", "Threshold", "UV Test" };
            int debugMode = static_cast<int>(m_VGRenderer->getHiZDebugMode());
            if (ImGui::Combo("Visualization", &debugMode, modeNames, 3)) {
                m_VGRenderer->setHiZDebugMode(static_cast<uint32_t>(debugMode));
            }
            if (debugMode == 0) {
                ImGui::Text("White=far(1.0), Black=near(0.0)");
            } else if (debugMode == 1) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "White = depth > 0.5");
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Red = 0.001 < depth < 0.5");
                ImGui::TextColored(ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "Blue = depth ~0 (BAD!)");
            } else {
                ImGui::Text("Shows UV gradient (ignores Hi-Z)");
            }
            ImGui::Text("Green bar on left = depth value");
        }

        ImGui::Unindent();
    }

    bool lodSelection = m_VGRenderer->isLodSelectionEnabled();
    if (ImGui::Checkbox("Auto LOD Selection", &lodSelection)) {
        m_VGRenderer->setLodSelectionEnabled(lodSelection);
    }

    float lodBias = m_VGRenderer->getLodBias();
    if (ImGui::SliderFloat("LOD Bias", &lodBias, 0.1f, 4.0f, "%.2f")) {
        m_VGRenderer->setLodBias(lodBias);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##LODBias")) {
        m_VGRenderer->setLodBias(1.0f);
    }

    float errorThreshold = m_VGRenderer->getErrorThreshold();
    if (ImGui::SliderFloat("Error Threshold##GPU", &errorThreshold, 0.1f, 10.0f, "%.1f px")) {
        m_VGRenderer->setErrorThreshold(errorThreshold);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##ErrorGPU")) {
        m_VGRenderer->setErrorThreshold(1.0f);
    }

    ImGui::Unindent();

    ImGui::Spacing();

    // Debug modes
    ImGui::Text("Debug Visualization:");
    ImGui::Indent();

    const char* debugModes[] = { "Normal", "Cluster Colors", "Normals", "LOD Levels" };
    int currentMode = static_cast<int>(m_VGRenderer->getDebugMode());
    if (ImGui::Combo("Mode", &currentMode, debugModes, 4)) {
        m_VGRenderer->setDebugMode(static_cast<uint32_t>(currentMode));
    }

    ImGui::Unindent();
}

} // namespace MiEngine
