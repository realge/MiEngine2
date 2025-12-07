#include "debug/SceneHierarchyPanel.h"
#include "VulkanRenderer.h"
#include "Utils/Picking.h"
#include "core/MiWorld.h"
#include "core/MiActor.h"
#include "actor/MiStaticMeshActor.h"

SceneHierarchyPanel::SceneHierarchyPanel(VulkanRenderer* renderer)
    : DebugPanel("Scene Hierarchy", renderer) {
}

void SceneHierarchyPanel::handlePicking(float mouseX, float mouseY) {
    // Try MiWorld first
    MiEngine::MiWorld* world = renderer->getWorld();
    if (world && world->isInitialized()) {
        // TODO: Implement picking for MiWorld actors
        return;
    }

    // Fall back to old Scene system
    Scene* scene = renderer->getScene();
    Camera* camera = renderer->getCamera();
    if (!scene || !camera) return;

    VkExtent2D extent = renderer->getSwapChainExtent();
    float screenWidth = static_cast<float>(extent.width);
    float screenHeight = static_cast<float>(extent.height);

    const auto& meshInstances = scene->getMeshInstances();
    int picked = Picking::pickMesh(mouseX, mouseY, screenWidth, screenHeight, camera, meshInstances);

    // Update selection (picked can be -1 to deselect when clicking empty space)
    selectedMeshIndex = picked;
}

void SceneHierarchyPanel::draw() {
    ImGui::SetNextWindowPos(ImVec2(780, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(panelName.c_str(), &isOpen)) {
        ImGui::TextDisabled("(Left-click in viewport to select mesh)");
        ImGui::TextDisabled("(Right-click + drag to move camera)");
        ImGui::Separator();

        // Check if we're using MiWorld or old Scene
        MiEngine::MiWorld* world = renderer->getWorld();
        bool usingWorld = world && world->isInitialized() && world->getActorCount() > 0;

        if (usingWorld) {
            // MiWorld-based hierarchy
            if (ImGui::CollapsingHeader("Actors", ImGuiTreeNodeFlags_DefaultOpen)) {
                drawActorList();
            }

            if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
                drawWorldLightList();
            }

            if (selectedMeshIndex >= 0) {
                ImGui::Separator();
                drawActorProperties();
            }
        } else {
            // Old Scene-based hierarchy
            if (ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
                drawMeshList();
            }

            if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
                drawLightList();
            }

            if (selectedMeshIndex >= 0) {
                ImGui::Separator();
                drawMeshProperties();
            }
        }
    }
    ImGui::End();
}

void SceneHierarchyPanel::drawActorList() {
    MiEngine::MiWorld* world = renderer->getWorld();
    if (!world) return;

    const auto& actors = world->getAllActors();

    for (size_t i = 0; i < actors.size(); i++) {
        const auto& actor = actors[i];
        bool isSelected = (selectedMeshIndex == static_cast<int>(i));

        std::string label = actor->getName();
        if (label.empty()) {
            label = "Actor " + std::to_string(i);
        }

        // Add type indicator
        std::string typeIndicator;
        if (std::dynamic_pointer_cast<MiEngine::MiStaticMeshActor>(actor)) {
            typeIndicator = " [Mesh]";
        } else {
            typeIndicator = " [Empty]";
        }
        label += typeIndicator;

        if (ImGui::Selectable(label.c_str(), isSelected)) {
            selectedMeshIndex = static_cast<int>(i);
        }
    }
}

void SceneHierarchyPanel::drawActorProperties() {
    MiEngine::MiWorld* world = renderer->getWorld();
    if (!world) return;

    const auto& actors = world->getAllActors();
    if (selectedMeshIndex < 0 || selectedMeshIndex >= static_cast<int>(actors.size())) {
        return;
    }

    auto actor = actors[selectedMeshIndex];
    if (!actor) return;

    ImGui::Text("Actor: %s", actor->getName().c_str());
    ImGui::Text("Type: %s", actor->getTypeName());
    ImGui::Separator();

    // Transform - Editable
    ImGui::Text("Transform");

    // Position
    glm::vec3 pos = actor->getPosition();
    float posArr[3] = { pos.x, pos.y, pos.z };
    if (ImGui::DragFloat3("Position", posArr, 0.1f)) {
        actor->setPosition(glm::vec3(posArr[0], posArr[1], posArr[2]));
    }

    // Rotation (Euler angles in degrees)
    glm::vec3 euler = actor->getEulerAngles();
    float rotDegrees[3] = {
        glm::degrees(euler.x),
        glm::degrees(euler.y),
        glm::degrees(euler.z)
    };
    if (ImGui::DragFloat3("Rotation", rotDegrees, 1.0f, -360.0f, 360.0f)) {
        actor->setEulerAngles(glm::vec3(
            glm::radians(rotDegrees[0]),
            glm::radians(rotDegrees[1]),
            glm::radians(rotDegrees[2])
        ));
    }

    // Scale
    glm::vec3 scl = actor->getScale();
    float sclArr[3] = { scl.x, scl.y, scl.z };
    if (ImGui::DragFloat3("Scale", sclArr, 0.01f, 0.001f, 100.0f)) {
        actor->setScale(glm::vec3(sclArr[0], sclArr[1], sclArr[2]));
    }

    // Reset and Deselect buttons
    if (ImGui::Button("Reset Transform")) {
        actor->setPosition(glm::vec3(0.0f));
        actor->setRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        actor->setScale(glm::vec3(1.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("Deselect")) {
        selectedMeshIndex = -1;
        return;
    }

    // Material info for MiStaticMeshActor
    auto meshActor = std::dynamic_pointer_cast<MiEngine::MiStaticMeshActor>(actor);
    if (meshActor) {
        ImGui::Separator();
        ImGui::Text("Material:");

        // Get material (mutable for editing)
        Material& material = meshActor->getMaterial();

        // Diffuse color (base color)
        float diffuseColor[3] = { material.diffuseColor.r, material.diffuseColor.g, material.diffuseColor.b };
        if (ImGui::ColorEdit3("Diffuse Color", diffuseColor)) {
            material.diffuseColor = glm::vec3(diffuseColor[0], diffuseColor[1], diffuseColor[2]);
        }

        // Metallic
        if (ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f)) {
            // Value updated directly
        }

        // Roughness
        if (ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f)) {
            // Value updated directly
        }

        // Show mesh path
        const std::string& meshPath = meshActor->getMeshAssetPath();
        if (!meshPath.empty()) {
            ImGui::Text("Mesh: %s", meshPath.c_str());
        }
    }
}

void SceneHierarchyPanel::drawWorldLightList() {
    MiEngine::MiWorld* world = renderer->getWorld();
    if (!world) return;

    const auto& lights = world->getLights();

    for (size_t i = 0; i < lights.size(); i++) {
        const auto& light = lights[i];

        std::string label = light.isDirectional ? "Directional Light " : "Point Light ";
        label += std::to_string(i);

        if (ImGui::TreeNode(label.c_str())) {
            if (light.isDirectional) {
                ImGui::Text("Direction: (%.2f, %.2f, %.2f)",
                           light.position.x, light.position.y, light.position.z);
            } else {
                ImGui::Text("Position: (%.2f, %.2f, %.2f)",
                           light.position.x, light.position.y, light.position.z);
            }
            ImGui::Text("Color: (%.2f, %.2f, %.2f)",
                       light.color.r, light.color.g, light.color.b);
            ImGui::Text("Intensity: %.2f", light.intensity);

            if (!light.isDirectional) {
                ImGui::Text("Radius: %.2f", light.radius);
                ImGui::Text("Falloff: %.2f", light.falloff);
            }

            ImGui::TreePop();
        }
    }

    if (lights.empty()) {
        ImGui::TextDisabled("No lights in world");
    }
}

void SceneHierarchyPanel::drawMeshList() {
    Scene* scene = renderer->getScene();
    if (!scene) return;

    const auto& meshInstances = scene->getMeshInstances();

    for (size_t i = 0; i < meshInstances.size(); i++) {
        bool isSelected = (selectedMeshIndex == static_cast<int>(i));

        std::string label = "Mesh " + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            selectedMeshIndex = static_cast<int>(i);
        }
    }
}

void SceneHierarchyPanel::drawMeshProperties() {
    Scene* scene = renderer->getScene();
    if (!scene) return;

    // Need non-const access to modify transform
    MeshInstance* instance = scene->getMeshInstance(static_cast<size_t>(selectedMeshIndex));
    if (!instance) return;

    ImGui::Text("Mesh %d Properties:", selectedMeshIndex);
    ImGui::Separator();

    // Transform - Editable
    ImGui::Text("Transform");

    // Position
    float pos[3] = { instance->transform.position.x, instance->transform.position.y, instance->transform.position.z };
    if (ImGui::DragFloat3("Position", pos, 0.1f)) {
        instance->transform.position = glm::vec3(pos[0], pos[1], pos[2]);
    }

    // Rotation (in radians, displayed as degrees for convenience)
    float rotDegrees[3] = {
        glm::degrees(instance->transform.rotation.x),
        glm::degrees(instance->transform.rotation.y),
        glm::degrees(instance->transform.rotation.z)
    };
    if (ImGui::DragFloat3("Rotation", rotDegrees, 1.0f, -360.0f, 360.0f)) {
        instance->transform.rotation = glm::vec3(
            glm::radians(rotDegrees[0]),
            glm::radians(rotDegrees[1]),
            glm::radians(rotDegrees[2])
        );
    }

    // Scale
    float scl[3] = { instance->transform.scale.x, instance->transform.scale.y, instance->transform.scale.z };
    if (ImGui::DragFloat3("Scale", scl, 0.01f, 0.001f, 100.0f)) {
        instance->transform.scale = glm::vec3(scl[0], scl[1], scl[2]);
    }

    // Reset and Deselect buttons
    if (ImGui::Button("Reset Transform")) {
        instance->transform.position = glm::vec3(0.0f);
        instance->transform.rotation = glm::vec3(0.0f);
        instance->transform.scale = glm::vec3(1.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Deselect")) {
        selectedMeshIndex = -1;
        return;
    }

    // Material info
    if (instance->mesh && instance->mesh->getMaterial()) {
        const auto& material = instance->mesh->getMaterial();
        ImGui::Separator();
        ImGui::Text("Material:");
        ImGui::Text("  Metallic: %.2f", material->metallic);
        ImGui::Text("  Roughness: %.2f", material->roughness);
        ImGui::Text("  Alpha: %.2f", material->alpha);
    }
}

void SceneHierarchyPanel::drawLightList() {
    Scene* scene = renderer->getScene();
    if (!scene) return;

    const auto& lights = scene->getLights();

    for (size_t i = 0; i < lights.size(); i++) {
        const auto& light = lights[i];

        std::string label = light.isDirectional ? "Directional Light " : "Point Light ";
        label += std::to_string(i);

        if (ImGui::TreeNode(label.c_str())) {
            ImGui::Text("Position: (%.2f, %.2f, %.2f)",
                       light.position.x, light.position.y, light.position.z);
            ImGui::Text("Color: (%.2f, %.2f, %.2f)",
                       light.color.r, light.color.g, light.color.b);
            ImGui::Text("Intensity: %.2f", light.intensity);

            if (!light.isDirectional) {
                ImGui::Text("Radius: %.2f", light.radius);
                ImGui::Text("Falloff: %.2f", light.falloff);
            }

            ImGui::TreePop();
        }
    }
}
