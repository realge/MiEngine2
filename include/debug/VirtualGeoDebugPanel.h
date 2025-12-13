#pragma once

#include "DebugPanel.h"
#include "include/virtualgeo/VirtualGeoTypes.h"
#include <memory>

namespace MiEngine {

struct ClusteredMesh;
struct ClusteringStats;
class VirtualGeoRenderer;

class VirtualGeoDebugPanel : public DebugPanel {
public:
    VirtualGeoDebugPanel(VulkanRenderer* renderer);
    virtual ~VirtualGeoDebugPanel() = default;

    void draw() override;

    // Set current clustered mesh for inspection
    void setClusteredMesh(ClusteredMesh* mesh) { m_ClusteredMesh = mesh; }

    // Set clustering stats
    void setClusteringStats(const ClusteringStats& stats) { m_Stats = stats; }

    // Set VirtualGeoRenderer for runtime controls
    void setVirtualGeoRenderer(VirtualGeoRenderer* vgRenderer) { m_VGRenderer = vgRenderer; }

    // Visualization options
    bool isClusterVisualizationEnabled() const { return m_ShowClusterColors; }
    bool isLODVisualizationEnabled() const { return m_ShowLODColors; }
    int getSelectedLOD() const { return m_SelectedLOD; }
    bool isWireframeEnabled() const { return m_ShowWireframe; }

    // Category for grouping in menus
    std::string getCategory() const { return "Rendering"; }

private:
    void renderClusterInfo();
    void renderLODSelector();
    void renderVisualizationOptions();
    void renderStats();
    void renderRendererControls();

    ClusteredMesh* m_ClusteredMesh = nullptr;
    ClusteringStats m_Stats{};
    VirtualGeoRenderer* m_VGRenderer = nullptr;

    // Visualization settings
    bool m_ShowClusterColors = true;
    bool m_ShowLODColors = false;
    bool m_ShowWireframe = false;
    bool m_ShowBoundingSpheres = false;
    int m_SelectedLOD = -1;  // -1 = auto LOD selection
    float m_LODErrorThreshold = 1.0f;
};

} // namespace MiEngine
