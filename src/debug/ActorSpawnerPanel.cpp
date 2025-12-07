#include "debug/ActorSpawnerPanel.h"
#include "VulkanRenderer.h"
#include "core/MiWorld.h"
#include "core/MiTypeRegistry.h"
#include "actor/MiEmptyActor.h"
#include "actor/MiStaticMeshActor.h"
#include "asset/AssetRegistry.h"
#include "camera/Camera.h"
#include "imgui.h"
#include <algorithm>

using namespace MiEngine;

ActorSpawnerPanel::ActorSpawnerPanel(VulkanRenderer* renderer)
    : DebugPanel("Actor Spawner", renderer) {
}

void ActorSpawnerPanel::draw() {
    if (!isOpen) return;

    // Initialize data on first draw
    if (!m_DataInitialized) {
        refreshActorTypes();
        refreshMeshList();
        m_DataInitialized = true;
    }

    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Actor Spawner", &isOpen)) {
        // Check if world exists
        MiWorld* world = renderer ? renderer->getWorld() : nullptr;
        if (!world) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No world available!");
            ImGui::End();
            return;
        }

        ImGui::Text("World: %s (%zu actors)", world->getName().c_str(), world->getActorCount());
        ImGui::Separator();

        // Quick spawn section
        drawQuickSpawn();

        ImGui::Separator();

        // Custom spawn section
        drawCustomSpawn();
    }
    ImGui::End();
}

void ActorSpawnerPanel::drawQuickSpawn() {
    ImGui::Text("Quick Spawn");
    ImGui::Spacing();

    float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;

    if (ImGui::Button("Empty Actor", ImVec2(buttonWidth, 30))) {
        spawnEmptyActor();
    }
    ImGui::SameLine();
    if (ImGui::Button("Static Mesh", ImVec2(buttonWidth, 30))) {
        spawnStaticMeshActor();
    }

    // Spawn position option
    ImGui::Spacing();
    ImGui::Checkbox("Spawn at Camera", &m_SpawnAtCamera);
    if (!m_SpawnAtCamera) {
        ImGui::DragFloat3("Position", m_SpawnPosition, 0.1f);
    }
}

void ActorSpawnerPanel::drawCustomSpawn() {
    ImGui::Text("Custom Spawn");
    ImGui::Spacing();

    // Actor name
    ImGui::Text("Name (optional):");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##ActorName", m_ActorNameBuffer, sizeof(m_ActorNameBuffer));

    // Actor type selector
    ImGui::Spacing();
    ImGui::Text("Actor Type:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##ActorType",
            m_ActorTypes.empty() ? "No types" : m_ActorTypes[m_SelectedActorType].c_str())) {
        for (int i = 0; i < static_cast<int>(m_ActorTypes.size()); ++i) {
            bool isSelected = (m_SelectedActorType == i);
            if (ImGui::Selectable(m_ActorTypes[i].c_str(), isSelected)) {
                m_SelectedActorType = i;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Mesh selector (for static mesh actors)
    drawMeshSelector();

    // Spawn button
    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Spawn Actor", ImVec2(-1, 35))) {
        if (!m_ActorTypes.empty()) {
            const std::string& typeName = m_ActorTypes[m_SelectedActorType];

            if (typeName == "MiStaticMeshActor" && !m_MeshPaths.empty()) {
                spawnStaticMeshActor(m_MeshPaths[m_SelectedMesh]);
            } else if (typeName == "MiEmptyActor") {
                spawnEmptyActor();
            } else {
                // Generic spawn via type registry
                MiWorld* world = renderer->getWorld();
                if (world) {
                    auto actor = world->spawnActorByTypeName(typeName);
                    if (actor) {
                        actor->setPosition(getSpawnPosition(m_SpawnAtCamera));
                        if (strlen(m_ActorNameBuffer) > 0) {
                            actor->setName(m_ActorNameBuffer);
                        }
                    }
                }
            }
        }
    }

    // Refresh button
    ImGui::Spacing();
    if (ImGui::Button("Refresh Lists", ImVec2(-1, 0))) {
        refreshActorTypes();
        refreshMeshList();
    }
}

void ActorSpawnerPanel::drawMeshSelector() {
    // Only show mesh selector for static mesh actors
    if (m_ActorTypes.empty() || m_ActorTypes[m_SelectedActorType] != "MiStaticMeshActor") {
        return;
    }

    ImGui::Spacing();
    ImGui::Text("Mesh:");
    ImGui::SetNextItemWidth(-1);

    const char* preview = m_MeshPaths.empty() ? "No meshes imported" : m_MeshPaths[m_SelectedMesh].c_str();
    if (ImGui::BeginCombo("##MeshSelector", preview)) {
        for (int i = 0; i < static_cast<int>(m_MeshPaths.size()); ++i) {
            bool isSelected = (m_SelectedMesh == i);
            if (ImGui::Selectable(m_MeshPaths[i].c_str(), isSelected)) {
                m_SelectedMesh = i;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (m_MeshPaths.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
            "Import meshes via Assets > Import Model");
    }
}

void ActorSpawnerPanel::spawnEmptyActor() {
    MiWorld* world = renderer ? renderer->getWorld() : nullptr;
    if (!world) return;

    auto actor = world->spawnActor<MiEmptyActor>();
    if (actor) {
        actor->setPosition(getSpawnPosition(m_SpawnAtCamera));
        if (strlen(m_ActorNameBuffer) > 0) {
            actor->setName(m_ActorNameBuffer);
        }
    }
}

void ActorSpawnerPanel::spawnStaticMeshActor(const std::string& meshPath) {
    MiWorld* world = renderer ? renderer->getWorld() : nullptr;
    if (!world) return;

    auto actor = world->spawnActor<MiStaticMeshActor>();
    if (actor) {
        actor->setPosition(getSpawnPosition(m_SpawnAtCamera));

        if (strlen(m_ActorNameBuffer) > 0) {
            actor->setName(m_ActorNameBuffer);
        }

        // Set mesh if provided
        if (!meshPath.empty()) {
            actor->setMesh(meshPath);
        } else if (!m_MeshPaths.empty()) {
            // Use selected mesh from list
            actor->setMesh(m_MeshPaths[m_SelectedMesh]);
        }
    }
}

glm::vec3 ActorSpawnerPanel::getSpawnPosition(bool atCamera) {
    if (atCamera && renderer) {
        Camera* camera = renderer->getCamera();
        if (camera) {
            // Spawn 5 units in front of camera
            glm::vec3 camPos = camera->getPosition();
            glm::vec3 camFront = camera->getFront();
            return camPos + camFront * 5.0f;
        }
    }
    return glm::vec3(m_SpawnPosition[0], m_SpawnPosition[1], m_SpawnPosition[2]);
}

void ActorSpawnerPanel::refreshActorTypes() {
    m_ActorTypes.clear();

    auto& registry = MiTypeRegistry::getInstance();
    auto types = registry.getRegisteredTypeNames();

    // Filter to only actor types (those that inherit from MiActor)
    for (const auto& typeName : types) {
        // Check if it's an actor type by looking at the name pattern
        // Actor types typically start with "Mi" and end with "Actor"
        if (typeName.find("Actor") != std::string::npos) {
            m_ActorTypes.push_back(typeName);
        }
    }

    // Sort alphabetically
    std::sort(m_ActorTypes.begin(), m_ActorTypes.end());

    // Reset selection if out of bounds
    if (m_SelectedActorType >= static_cast<int>(m_ActorTypes.size())) {
        m_SelectedActorType = 0;
    }
}

void ActorSpawnerPanel::refreshMeshList() {
    m_MeshPaths.clear();

    auto& registry = AssetRegistry::getInstance();
    const auto& assets = registry.getAssets();

    for (const auto& entry : assets) {
        // Only include mesh assets (static and skeletal)
        if (entry.type == AssetType::StaticMesh || entry.type == AssetType::SkeletalMesh) {
            m_MeshPaths.push_back(entry.projectPath);
        }
    }

    // Sort alphabetically
    std::sort(m_MeshPaths.begin(), m_MeshPaths.end());

    // Reset selection if out of bounds
    if (m_SelectedMesh >= static_cast<int>(m_MeshPaths.size())) {
        m_SelectedMesh = 0;
    }
}
