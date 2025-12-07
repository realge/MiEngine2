#pragma once
#include "DebugPanel.h"

class SceneHierarchyPanel : public DebugPanel {
public:
    SceneHierarchyPanel(VulkanRenderer* renderer);
    void draw() override;

    // Called to update picking based on mouse click
    void handlePicking(float mouseX, float mouseY);

    // Get/Set selected mesh index (for external access)
    int getSelectedMeshIndex() const { return selectedMeshIndex; }
    void setSelectedMeshIndex(int index) { selectedMeshIndex = index; }

private:
    int selectedMeshIndex = -1;

    // MiWorld-based drawing (new actor system)
    void drawActorList();
    void drawActorProperties();
    void drawWorldLightList();

    // Old Scene-based drawing (legacy)
    void drawMeshList();
    void drawMeshProperties();
    void drawLightList();
};
