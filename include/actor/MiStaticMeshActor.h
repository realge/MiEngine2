#pragma once

#include "core/MiActor.h"
#include "component/MiStaticMeshComponent.h"
#include <memory>
#include <string>

// Forward declarations
class Mesh;

namespace MiEngine {

// Actor containing a static mesh
// Similar to AStaticMeshActor in UE5
class MiStaticMeshActor : public MiActor {
    MI_OBJECT_BODY(MiStaticMeshActor, 101)

public:
    MiStaticMeshActor();
    virtual ~MiStaticMeshActor() = default;

    // ========================================================================
    // Mesh Component Access
    // ========================================================================

    std::shared_ptr<MiStaticMeshComponent> getMeshComponent() const { return m_MeshComponent; }

    // ========================================================================
    // Quick Setup Methods
    // ========================================================================

    // Set mesh by shared_ptr
    void setMesh(std::shared_ptr<Mesh> mesh);

    // Set mesh by asset path
    void setMesh(const std::string& assetPath);

    // Get mesh
    std::shared_ptr<Mesh> getMesh() const;

    // Get mesh asset path
    const std::string& getMeshAssetPath() const;

    // ========================================================================
    // Material
    // ========================================================================

    void setMaterial(const Material& material);
    const Material& getMaterial() const;
    Material& getMaterial();

    // Convenience setters
    void setBaseColor(const glm::vec3& color);
    void setMetallic(float metallic);
    void setRoughness(float roughness);

    // ========================================================================
    // Shadow Settings
    // ========================================================================

    bool getCastShadows() const;
    void setCastShadows(bool cast);

    bool getReceiveShadows() const;
    void setReceiveShadows(bool receive);

    // ========================================================================
    // Serialization
    // ========================================================================

    void serialize(JsonWriter& writer) const override;
    void deserialize(const JsonReader& reader) override;

    // Create default mesh component
    void createDefaultComponents() override;

private:
    std::shared_ptr<MiStaticMeshComponent> m_MeshComponent;
};

} // namespace MiEngine
