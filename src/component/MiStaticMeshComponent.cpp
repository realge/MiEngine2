#include "component/MiStaticMeshComponent.h"
#include "core/MiTypeRegistry.h"
#include "core/JsonIO.h"
#include "core/MiActor.h"
#include "core/MiWorld.h"
#include "mesh/Mesh.h"
#include "asset/MeshLibrary.h"
#include "VulkanRenderer.h"

namespace MiEngine {

MiStaticMeshComponent::MiStaticMeshComponent()
    : MiSceneComponent()
    , m_Mesh(nullptr)
    , m_MeshAssetPath("")
    , m_CastShadows(true)
    , m_ReceiveShadows(true)
    , m_LocalBoundsMin(-0.5f)
    , m_LocalBoundsMax(0.5f)
{
    setName("StaticMeshComponent");
}

// ============================================================================
// Mesh
// ============================================================================

void MiStaticMeshComponent::setMesh(std::shared_ptr<Mesh> mesh) {
    m_Mesh = mesh;
    updateBoundsFromMesh();
    markDirty();
}

void MiStaticMeshComponent::setMeshByPath(const std::string& assetPath) {
    m_MeshAssetPath = assetPath;

    // If already registered to world, load immediately
    if (getOwner() && getOwner()->getWorld()) {
        loadMeshFromPath();
    }
    // Otherwise, mesh will be loaded in onRegister()

    markDirty();
}

void MiStaticMeshComponent::onRegister() {
    MiSceneComponent::onRegister();

    // Load mesh if path is set but mesh isn't loaded
    if (!m_Mesh && !m_MeshAssetPath.empty()) {
        loadMeshFromPath();
    }
}

void MiStaticMeshComponent::loadMeshFromPath() {
    if (m_MeshAssetPath.empty()) return;

    MiActor* owner = getOwner();
    if (!owner) return;

    MiWorld* world = owner->getWorld();
    if (!world) return;

    VulkanRenderer* renderer = world->getRenderer();
    if (!renderer) return;

    MeshLibrary& meshLib = renderer->getMeshLibrary();
    m_Mesh = meshLib.getMesh(m_MeshAssetPath);

    if (m_Mesh) {
        updateBoundsFromMesh();
    }
}

void MiStaticMeshComponent::updateBoundsFromMesh() {
    if (m_Mesh) {
        // Get bounds from mesh vertices
        // For now, use default bounds - mesh should provide this
        // m_LocalBoundsMin = m_Mesh->getBoundsMin();
        // m_LocalBoundsMax = m_Mesh->getBoundsMax();
    }
}

// ============================================================================
// Material
// ============================================================================

void MiStaticMeshComponent::setMaterial(const Material& material) {
    m_Material = material;
    markDirty();
}

void MiStaticMeshComponent::setBaseColor(const glm::vec3& color) {
    m_Material.diffuseColor = color;
    markDirty();
}

void MiStaticMeshComponent::setMetallic(float metallic) {
    m_Material.metallic = metallic;
    markDirty();
}

void MiStaticMeshComponent::setRoughness(float roughness) {
    m_Material.roughness = roughness;
    markDirty();
}

// ============================================================================
// Rendering
// ============================================================================

bool MiStaticMeshComponent::shouldRender() const {
    return isEnabled() && isVisibleInHierarchy() && m_Mesh != nullptr;
}

glm::vec3 MiStaticMeshComponent::getLocalBoundsMin() const {
    return m_LocalBoundsMin;
}

glm::vec3 MiStaticMeshComponent::getLocalBoundsMax() const {
    return m_LocalBoundsMax;
}

// ============================================================================
// Serialization
// ============================================================================

void MiStaticMeshComponent::serialize(JsonWriter& writer) const {
    MiSceneComponent::serialize(writer);

    // Mesh asset path
    writer.writeString("meshAsset", m_MeshAssetPath);

    // Shadow settings
    writer.writeBool("castShadows", m_CastShadows);
    writer.writeBool("receiveShadows", m_ReceiveShadows);

    // Material (simplified - full PBR would have texture paths)
    writer.beginObject("material");
    writer.writeVec3("baseColor", m_Material.diffuseColor);
    writer.writeFloat("metallic", m_Material.metallic);
    writer.writeFloat("roughness", m_Material.roughness);
    writer.writeFloat("emissiveStrength", m_Material.emissiveStrength);
    writer.endObject();
}

void MiStaticMeshComponent::deserialize(const JsonReader& reader) {
    MiSceneComponent::deserialize(reader);

    // Mesh asset path
    m_MeshAssetPath = reader.getString("meshAsset", "");
    if (!m_MeshAssetPath.empty()) {
        // If already registered to world (loading after spawn), load mesh immediately
        if (getOwner() && getOwner()->getWorld()) {
            loadMeshFromPath();
        }
    }

    // Shadow settings
    m_CastShadows = reader.getBool("castShadows", true);
    m_ReceiveShadows = reader.getBool("receiveShadows", true);

    // Material
    JsonReader materialReader = reader.getObject("material");
    if (materialReader.isValid()) {
        m_Material.diffuseColor = materialReader.getVec3("baseColor", glm::vec3(1.0f));
        m_Material.metallic = materialReader.getFloat("metallic", 0.0f);
        m_Material.roughness = materialReader.getFloat("roughness", 0.5f);
        m_Material.emissiveStrength = materialReader.getFloat("emissiveStrength", 0.0f);
    }
}

// Register the type
MI_REGISTER_TYPE(MiStaticMeshComponent)

} // namespace MiEngine
