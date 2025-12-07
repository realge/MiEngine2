#include "actor/MiStaticMeshActor.h"
#include "core/MiTypeRegistry.h"
#include "core/JsonIO.h"

namespace MiEngine {

MiStaticMeshActor::MiStaticMeshActor()
    : MiActor()
    , m_MeshComponent(nullptr)
{
    setName("StaticMeshActor");
}

void MiStaticMeshActor::createDefaultComponents() {
    // Create the mesh component as both root and mesh
    m_MeshComponent = addComponent<MiStaticMeshComponent>();
    m_MeshComponent->setName("StaticMeshComponent");

    // Set as root component
    setRootComponent(m_MeshComponent);
}

// ============================================================================
// Mesh
// ============================================================================

void MiStaticMeshActor::setMesh(std::shared_ptr<Mesh> mesh) {
    if (m_MeshComponent) {
        m_MeshComponent->setMesh(mesh);
    }
}

void MiStaticMeshActor::setMesh(const std::string& assetPath) {
    if (m_MeshComponent) {
        m_MeshComponent->setMeshByPath(assetPath);
    }
}

std::shared_ptr<Mesh> MiStaticMeshActor::getMesh() const {
    return m_MeshComponent ? m_MeshComponent->getMesh() : nullptr;
}

const std::string& MiStaticMeshActor::getMeshAssetPath() const {
    static const std::string empty;
    return m_MeshComponent ? m_MeshComponent->getMeshAssetPath() : empty;
}

// ============================================================================
// Material
// ============================================================================

void MiStaticMeshActor::setMaterial(const Material& material) {
    if (m_MeshComponent) {
        m_MeshComponent->setMaterial(material);
    }
}

const Material& MiStaticMeshActor::getMaterial() const {
    static Material defaultMaterial;
    return m_MeshComponent ? m_MeshComponent->getMaterial() : defaultMaterial;
}

Material& MiStaticMeshActor::getMaterial() {
    static Material defaultMaterial;
    return m_MeshComponent ? m_MeshComponent->getMaterial() : defaultMaterial;
}

void MiStaticMeshActor::setBaseColor(const glm::vec3& color) {
    if (m_MeshComponent) {
        m_MeshComponent->setBaseColor(color);
    }
}

void MiStaticMeshActor::setMetallic(float metallic) {
    if (m_MeshComponent) {
        m_MeshComponent->setMetallic(metallic);
    }
}

void MiStaticMeshActor::setRoughness(float roughness) {
    if (m_MeshComponent) {
        m_MeshComponent->setRoughness(roughness);
    }
}

// ============================================================================
// Shadows
// ============================================================================

bool MiStaticMeshActor::getCastShadows() const {
    return m_MeshComponent ? m_MeshComponent->getCastShadows() : true;
}

void MiStaticMeshActor::setCastShadows(bool cast) {
    if (m_MeshComponent) {
        m_MeshComponent->setCastShadows(cast);
    }
}

bool MiStaticMeshActor::getReceiveShadows() const {
    return m_MeshComponent ? m_MeshComponent->getReceiveShadows() : true;
}

void MiStaticMeshActor::setReceiveShadows(bool receive) {
    if (m_MeshComponent) {
        m_MeshComponent->setReceiveShadows(receive);
    }
}

// ============================================================================
// Serialization
// ============================================================================

void MiStaticMeshActor::serialize(JsonWriter& writer) const {
    // Base actor handles most serialization including components
    MiActor::serialize(writer);
}

void MiStaticMeshActor::deserialize(const JsonReader& reader) {
    // Base actor handles deserialization
    MiActor::deserialize(reader);

    // After deserialization, find the mesh component
    m_MeshComponent = getComponent<MiStaticMeshComponent>();
}

// Register the type
MI_REGISTER_TYPE(MiStaticMeshActor)

} // namespace MiEngine
