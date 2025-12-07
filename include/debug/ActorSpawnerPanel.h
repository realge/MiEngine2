#pragma once

#include "debug/DebugPanel.h"
#include <string>
#include <vector>
#include <glm/vec3.hpp>

namespace MiEngine {
    class MiWorld;
    class MiActor;
}

class ActorSpawnerPanel : public DebugPanel {
public:
    ActorSpawnerPanel(VulkanRenderer* renderer);

    void draw() override;

private:
    // UI sections
    void drawQuickSpawn();
    void drawCustomSpawn();
    void drawMeshSelector();

    // Spawn helpers
    void spawnEmptyActor();
    void spawnStaticMeshActor(const std::string& meshPath = "");

    glm::vec3 getSpawnPosition(bool atCamera);

    // Refresh available data
    void refreshActorTypes();
    void refreshMeshList();

    // Cached data
    std::vector<std::string> m_ActorTypes;
    std::vector<std::string> m_MeshPaths;

    // UI state
    int m_SelectedActorType = 0;
    int m_SelectedMesh = 0;
    float m_SpawnPosition[3] = {0.0f, 0.0f, 0.0f};
    char m_ActorNameBuffer[128] = "";

    bool m_SpawnAtCamera = true;
    bool m_DataInitialized = false;
};
