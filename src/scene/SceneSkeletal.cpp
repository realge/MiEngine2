// Scene skeletal mesh integration
// This file contains skeletal model loading and animation methods for Scene

#include "scene/Scene.h"
#include "VulkanRenderer.h"
#include "animation/Skeleton.h"
#include "animation/AnimationClip.h"
#include "mesh/SkeletalMesh.h"
#include <iostream>
#include <filesystem>

using namespace MiEngine;

bool Scene::loadSkeletalModel(const std::string& filename, const Transform& transform) {
    SkeletalModelData modelData;
    if (!modelLoader.LoadSkeletalModel(filename, modelData)) {
        std::cerr << "Failed to load skeletal model: " << filename << std::endl;
        return false;
    }

    if (modelData.meshes.empty()) {
        std::cerr << "No meshes found in skeletal model: " << filename << std::endl;
        return false;
    }

    // Store the skeletal model data
    m_skeletalModels.push_back(modelData);

    // Create default material
    auto defaultMaterial = std::make_shared<Material>();
    VkDescriptorSet materialDescriptorSet = renderer->createMaterialDescriptorSet(*defaultMaterial);
    if (materialDescriptorSet != VK_NULL_HANDLE) {
        defaultMaterial->setDescriptorSet(materialDescriptorSet);
    }

    // Create mesh instances for each skeletal mesh
    for (const auto& skeletalMeshData : modelData.meshes) {
        // Create SkeletalMesh (GPU buffers for skeletal vertex data)
        auto mesh = std::make_shared<SkeletalMesh>(
            renderer->getDevice(),
            renderer->getPhysicalDevice(),
            skeletalMeshData,
            defaultMaterial
        );
        mesh->createBuffers(renderer->getCommandPool(), renderer->getGraphicsQueue());

        // Create mesh instance
        MeshInstance instance(mesh, transform);
        instance.isSkeletal = true;
        instance.instanceId = m_NextInstanceId++;

        // Create skeletal mesh component with animation support
        if (modelData.skeleton) {
            instance.skeletalMesh = std::make_shared<SkeletalMeshComponent>(modelData.skeleton);

            // Auto-play animation - prefer "Take 001" or last animation (skip T-pose references)
            if (!modelData.animations.empty()) {
                std::shared_ptr<AnimationClip> animToPlay = nullptr;

                // Try to find "Take 001" or similar actual animation
                for (const auto& anim : modelData.animations) {
                    if (anim->getName().find("mixamo.com") != std::string::npos) {
                        animToPlay = anim;
                        break;
                    }
                }

                // If no "Take" found, use last animation (first is often T-pose)
                if (!animToPlay && modelData.animations.size() > 1) {
                    animToPlay = modelData.animations.back();
                } else if (!animToPlay) {
                    animToPlay = modelData.animations[0];
                }

                instance.skeletalMesh->playAnimation(animToPlay, true);
            }
        }

        meshInstances.push_back(std::move(instance));
    }

    return true;
}

bool Scene::loadSkeletalModelPBR(const std::string& modelFilename,
                                  const MaterialTexturePaths& texturePaths,
                                  const Transform& transform) {
    SkeletalModelData modelData;
    if (!modelLoader.LoadSkeletalModel(modelFilename, modelData)) {
        std::cerr << "Failed to load skeletal model: " << modelFilename << std::endl;
        return false;
    }

    if (modelData.meshes.empty()) {
        std::cerr << "No meshes found in skeletal model: " << modelFilename << std::endl;
        return false;
    }

    // Store the skeletal model data
    m_skeletalModels.push_back(modelData);

    // Create PBR material with textures
    auto material = std::make_shared<Material>();

    // Load textures
    if (!texturePaths.diffuse.empty()) {
        auto texture = loadTexture(texturePaths.diffuse);
        if (texture) material->setTexture(TextureType::Diffuse, texture);
    }

    if (!texturePaths.normal.empty()) {
        auto texture = loadTexture(texturePaths.normal);
        if (texture) material->setTexture(TextureType::Normal, texture);
    }

    // Handle metallic/roughness
    std::shared_ptr<Texture> metallicTex = nullptr;
    std::shared_ptr<Texture> roughnessTex = nullptr;

    if (!texturePaths.metallic.empty()) {
        metallicTex = loadTexture(texturePaths.metallic);
    }

    if (!texturePaths.roughness.empty()) {
        roughnessTex = loadTexture(texturePaths.roughness);
    }

    if (metallicTex || roughnessTex) {
        std::shared_ptr<Texture> combinedTex = TextureUtils::combineMetallicRoughness(
            renderer->getDevice(),
            renderer->getPhysicalDevice(),
            renderer->getCommandPool(),
            renderer->getGraphicsQueue(),
            metallicTex,
            roughnessTex
        );

        if (combinedTex) {
            material->setTexture(TextureType::MetallicRoughness, combinedTex);
        }
    }

    if (!texturePaths.ambientOcclusion.empty()) {
        auto texture = loadTexture(texturePaths.ambientOcclusion);
        if (texture) material->setTexture(TextureType::AmbientOcclusion, texture);
    }

    if (!texturePaths.emissive.empty()) {
        auto texture = loadTexture(texturePaths.emissive);
        if (texture) material->setTexture(TextureType::Emissive, texture);
    }

    // Set default properties
    material->diffuseColor = glm::vec3(1.0f);
    material->metallic = 1.0f;
    material->roughness = 1.0f;
    material->alpha = 1.0f;

    // Create descriptor set
    VkDescriptorSet materialDescriptorSet = renderer->createMaterialDescriptorSet(*material);
    if (materialDescriptorSet != VK_NULL_HANDLE) {
        material->setDescriptorSet(materialDescriptorSet);
    }

    // Create mesh instances for each skeletal mesh
    for (const auto& skeletalMeshData : modelData.meshes) {
        auto mesh = std::make_shared<SkeletalMesh>(
            renderer->getDevice(),
            renderer->getPhysicalDevice(),
            skeletalMeshData,
            material
        );
        mesh->createBuffers(renderer->getCommandPool(), renderer->getGraphicsQueue());

        MeshInstance instance(mesh, transform);
        instance.isSkeletal = true;
        instance.instanceId = m_NextInstanceId++;

        if (modelData.skeleton) {
            instance.skeletalMesh = std::make_shared<SkeletalMeshComponent>(modelData.skeleton);

            if (!modelData.animations.empty()) {
                instance.skeletalMesh->playAnimation(modelData.animations[0], true);
            }
        }

        meshInstances.push_back(std::move(instance));
    }

    return true;
}

void Scene::playAnimation(size_t instanceIndex, size_t animationIndex, bool loop) {
    if (instanceIndex >= meshInstances.size()) {
        std::cerr << "Invalid instance index: " << instanceIndex << std::endl;
        return;
    }

    MeshInstance* instance = &meshInstances[instanceIndex];
    if (!instance->skeletalMesh) {
        std::cerr << "Instance " << instanceIndex << " is not a skeletal mesh" << std::endl;
        return;
    }

    // Find the skeletal model that owns this skeleton
    for (const auto& model : m_skeletalModels) {
        if (model.skeleton == instance->skeletalMesh->getSkeleton()) {
            if (animationIndex < model.animations.size()) {
                instance->skeletalMesh->playAnimation(model.animations[animationIndex], loop);
                return;
            }
        }
    }

    std::cerr << "Animation index " << animationIndex << " not found" << std::endl;
}

void Scene::playAnimation(MeshInstance* instance, std::shared_ptr<AnimationClip> clip, bool loop) {
    if (!instance) {
        std::cerr << "Null instance" << std::endl;
        return;
    }

    if (!instance->skeletalMesh) {
        std::cerr << "Instance is not a skeletal mesh" << std::endl;
        return;
    }

    if (!clip) {
        std::cerr << "Null animation clip" << std::endl;
        return;
    }

    instance->skeletalMesh->playAnimation(clip, loop);
}
