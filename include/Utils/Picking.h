#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "mesh/Mesh.h"
#include "scene/Scene.h"
#include "camera/Camera.h"
#include <limits>

namespace Picking {

// Ray structure for picking
struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

// Ray-AABB intersection test (returns distance, or -1 if no hit)
inline float rayAABBIntersection(const Ray& ray, const AABB& aabb, const glm::mat4& modelMatrix) {
    // Transform AABB to world space by transforming corners and creating new AABB
    glm::vec3 worldMin = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 worldMax = glm::vec3(std::numeric_limits<float>::lowest());

    // Transform all 8 corners of the AABB
    glm::vec3 corners[8] = {
        glm::vec3(aabb.min.x, aabb.min.y, aabb.min.z),
        glm::vec3(aabb.max.x, aabb.min.y, aabb.min.z),
        glm::vec3(aabb.min.x, aabb.max.y, aabb.min.z),
        glm::vec3(aabb.max.x, aabb.max.y, aabb.min.z),
        glm::vec3(aabb.min.x, aabb.min.y, aabb.max.z),
        glm::vec3(aabb.max.x, aabb.min.y, aabb.max.z),
        glm::vec3(aabb.min.x, aabb.max.y, aabb.max.z),
        glm::vec3(aabb.max.x, aabb.max.y, aabb.max.z)
    };

    for (int i = 0; i < 8; i++) {
        glm::vec4 worldCorner = modelMatrix * glm::vec4(corners[i], 1.0f);
        worldMin = glm::min(worldMin, glm::vec3(worldCorner));
        worldMax = glm::max(worldMax, glm::vec3(worldCorner));
    }

    // Slab method for ray-AABB intersection
    glm::vec3 invDir = 1.0f / ray.direction;

    float t1 = (worldMin.x - ray.origin.x) * invDir.x;
    float t2 = (worldMax.x - ray.origin.x) * invDir.x;
    float t3 = (worldMin.y - ray.origin.y) * invDir.y;
    float t4 = (worldMax.y - ray.origin.y) * invDir.y;
    float t5 = (worldMin.z - ray.origin.z) * invDir.z;
    float t6 = (worldMax.z - ray.origin.z) * invDir.z;

    float tmin = glm::max(glm::max(glm::min(t1, t2), glm::min(t3, t4)), glm::min(t5, t6));
    float tmax = glm::min(glm::min(glm::max(t1, t2), glm::max(t3, t4)), glm::max(t5, t6));

    // If tmax < 0, ray is intersecting AABB but whole AABB is behind us
    if (tmax < 0) {
        return -1.0f;
    }

    // If tmin > tmax, ray doesn't intersect AABB
    if (tmin > tmax) {
        return -1.0f;
    }

    return tmin >= 0 ? tmin : tmax;
}

// Generate a ray from screen coordinates
inline Ray screenToRay(float mouseX, float mouseY, float screenWidth, float screenHeight,
                       const glm::mat4& viewMatrix, const glm::mat4& projMatrix) {
    // Convert screen coordinates to NDC (-1 to 1)
    float x = (2.0f * mouseX) / screenWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenHeight; // Flip Y for Vulkan

    // Create clip space position
    glm::vec4 rayClip(x, y, -1.0f, 1.0f);

    // Convert to eye/view space
    glm::mat4 invProj = glm::inverse(projMatrix);
    glm::vec4 rayEye = invProj * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

    // Convert to world space
    glm::mat4 invView = glm::inverse(viewMatrix);
    glm::vec4 rayWorld = invView * rayEye;

    Ray ray;
    ray.origin = glm::vec3(invView[3]); // Camera position
    ray.direction = glm::normalize(glm::vec3(rayWorld));

    return ray;
}

// Pick a mesh from screen coordinates, returns index or -1 if nothing hit
inline int pickMesh(float mouseX, float mouseY, float screenWidth, float screenHeight,
                    const Camera* camera, const std::vector<MeshInstance>& meshInstances) {
    if (!camera) return -1;

    float aspectRatio = screenWidth / screenHeight;
    glm::mat4 view = camera->getViewMatrix();
    glm::mat4 proj = camera->getProjectionMatrix(aspectRatio, camera->getNearPlane(), camera->getFarPlane());

    Ray ray = screenToRay(mouseX, mouseY, screenWidth, screenHeight, view, proj);

    int closestMesh = -1;
    float closestDist = std::numeric_limits<float>::max();

    for (size_t i = 0; i < meshInstances.size(); i++) {
        const auto& instance = meshInstances[i];
        if (!instance.mesh) continue;

        glm::mat4 modelMatrix = instance.transform.getModelMatrix();
        const AABB& aabb = instance.mesh->getBoundingBox();

        float dist = rayAABBIntersection(ray, aabb, modelMatrix);
        if (dist >= 0 && dist < closestDist) {
            closestDist = dist;
            closestMesh = static_cast<int>(i);
        }
    }

    return closestMesh;
}

} // namespace Picking
