#pragma once

#include "core/MiSceneComponent.h"
#include "material/Material.h"
#include <memory>
#include <string>

// Forward declarations
class Mesh;

namespace MiEngine {

class JsonWriter;
class JsonReader;

// Component that renders a static mesh
// Similar to UStaticMeshComponent in UE5
class MiStaticMeshComponent : public MiSceneComponent {
    MI_OBJECT_BODY(MiStaticMeshComponent, 210)

public:
    MiStaticMeshComponent();
    virtual ~MiStaticMeshComponent() = default;

    // ========================================================================
    // Mesh
    // ========================================================================

    // Get the mesh
    std::shared_ptr<Mesh> getMesh() const { return m_Mesh; }

    // Set mesh directly
    void setMesh(std::shared_ptr<Mesh> mesh);

    // Set mesh by asset path (will load via MeshLibrary)
    void setMeshByPath(const std::string& assetPath);

    // Get the asset path (for serialization)
    const std::string& getMeshAssetPath() const { return m_MeshAssetPath; }

    // Check if mesh is loaded
    bool hasMesh() const { return m_Mesh != nullptr; }

    // ========================================================================
    // Lifecycle
    // ========================================================================

    // Called when owner actor is registered to world - loads mesh from path
    void onRegister() override;

    // ========================================================================
    // Material
    // ========================================================================

    // Get the material
    const Material& getMaterial() const { return m_Material; }
    Material& getMaterial() { return m_Material; }

    // Set material
    void setMaterial(const Material& material);

    // Material properties shortcuts
    void setBaseColor(const glm::vec3& color);
    void setMetallic(float metallic);
    void setRoughness(float roughness);

    // ========================================================================
    // Rendering
    // ========================================================================

    // Check if component should be rendered
    bool shouldRender() const;

    // Get bounds (override for accurate mesh bounds)
    glm::vec3 getLocalBoundsMin() const override;
    glm::vec3 getLocalBoundsMax() const override;

    // Cast shadows
    bool getCastShadows() const { return m_CastShadows; }
    void setCastShadows(bool cast) { m_CastShadows = cast; }

    // Receive shadows
    bool getReceiveShadows() const { return m_ReceiveShadows; }
    void setReceiveShadows(bool receive) { m_ReceiveShadows = receive; }

    // ========================================================================
    // Serialization
    // ========================================================================

    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

private:
    std::shared_ptr<Mesh> m_Mesh;
    std::string m_MeshAssetPath;
    Material m_Material;

    bool m_CastShadows = true;
    bool m_ReceiveShadows = true;

    // Cached bounds from mesh
    glm::vec3 m_LocalBoundsMin = glm::vec3(-0.5f);
    glm::vec3 m_LocalBoundsMax = glm::vec3(0.5f);

    void updateBoundsFromMesh();
    void loadMeshFromPath();
};

} // namespace MiEngine
