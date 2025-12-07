#pragma once
#include "core/Game.h"
#include "core/MiWorld.h"
#include "actor/MiStaticMeshActor.h"
#include "VulkanRenderer.h"
#include "raytracing/RayTracingSystem.h"
#include <iostream>

class RayTracingTestGame : public Game {
public:
    void OnInit() override {
        std::cout << "Ray Tracing Test Mode Initialized" << std::endl;

        if (!m_Renderer) {
            std::cerr << "Error: No renderer available!" << std::endl;
            return;
        }

        if (!m_World) {
            std::cerr << "Error: No world available!" << std::endl;
            return;
        }

        // Check RT support
        MiEngine::RayTracingSystem* rtSystem = m_Renderer->getRayTracingSystem();
        if (!rtSystem) {
            std::cerr << "Warning: Ray Tracing System not available!" << std::endl;
        } else if (!rtSystem->isSupported()) {
            std::cerr << "Warning: Ray Tracing not supported on this hardware!" << std::endl;
            const auto& support = rtSystem->getFeatureSupport();
            std::cerr << "Reason: " << support.unsupportedReason << std::endl;
        } else {
            std::cout << "Ray Tracing Hardware: SUPPORTED" << std::endl;

            // Enable ray tracing
            rtSystem->getSettings().enabled = true;
            rtSystem->getSettings().enableReflections = true;
            rtSystem->getSettings().enableSoftShadows = true;
            rtSystem->getSettings().enableDenoising = true;
            rtSystem->getSettings().samplesPerPixel = 1;
            rtSystem->getSettings().maxBounces = 2;

            // Mark TLAS dirty to trigger rebuild
            rtSystem->markTLASDirty();

            std::cout << "Ray Tracing ENABLED with reflections, soft shadows, and denoising" << std::endl;
        }

        // Set render mode to PBR_IBL for best RT results
        m_Renderer->setRenderMode(RenderMode::PBR_IBL);

        // Setup environment (HDR for IBL reflections)
        m_World->setupEnvironment("hdr/test.hdr");

        // Setup lighting
        m_World->clearLights();
        m_World->addLight(
            glm::vec3(-0.5f, -1.0f, -0.3f),  // Direction
            glm::vec3(1.0f, 0.95f, 0.9f),     // Warm sunlight
            2.0f,                              // Intensity
            0.0f,                              // Radius (0 for directional)
            1.0f,                              // Falloff
            true                               // isDirectional
        );

        // Setup camera
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(8.0f, 5.0f, 8.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 1.0f, 0.0f));
            m_Camera->setFarPlane(200.0f);
            m_Camera->setFOV(60.0f);
        }

        // Create test scene with reflective surfaces using MiWorld
        CreateReflectionTestScene();
    }

    void OnUpdate(float deltaTime) override {
        m_Time += deltaTime;

        // Update world if playing
        if (m_World && m_World->isPlaying()) {
            m_World->tick(deltaTime);
        }
    }

    void OnRender() override {
        // Rendering handled by VulkanRenderer
    }

    void OnShutdown() override {
        std::cout << "Ray Tracing Test Mode Shutdown" << std::endl;
    }

private:
    float m_Time = 0.0f;

    // Helper to create a static mesh actor with PBR material
    std::shared_ptr<MiEngine::MiStaticMeshActor> CreatePBRActor(
        const std::string& name,
        const std::string& meshType,
        const glm::vec3& position,
        const glm::vec3& scale,
        const glm::vec3& rotation,
        const glm::vec3& color,
        float metallic,
        float roughness)
    {
        if (!m_World) return nullptr;

        auto actor = m_World->spawnActor<MiEngine::MiStaticMeshActor>();
        actor->setName(name);
        actor->setPosition(position);
        actor->setScale(scale);
        actor->setRotation(glm::quat(glm::radians(rotation)));

        // Set mesh type (primitive shapes)
        actor->setMesh(meshType);

        // Set material properties
        actor->setBaseColor(color);
        actor->setMetallic(metallic);
        actor->setRoughness(roughness);

        return actor;
    }

    void CreateReflectionTestScene() {
        if (!m_World) {
            std::cerr << "Error: No world available!" << std::endl;
            return;
        }

        // Start world simulation
        m_World->beginPlay();

        // ===================================================================
        // Reflection test scene: reflective plane + cubes + spheres
        // ===================================================================

        // Ground Plane - Reflective mirror surface
        CreatePBRActor(
            "MirrorFloor",
            "plane",
            glm::vec3(0.0f, -0.5f, 0.0f),
            glm::vec3(20.0f, 1.0f, 20.0f),
            glm::vec3(0.0f),
            glm::vec3(0.8f, 0.8f, 0.85f),  // Light silver
            0.9f,   // Highly metallic - REFLECTIVE
            0.05f   // Smooth - REFLECTIVE
        );

        // Mirror Sphere - Should reflect everything
        CreatePBRActor(
            "MirrorSphere",
            "sphere",
            glm::vec3(-2.0f, 0.5f, 0.0f),
            glm::vec3(1.0f),
            glm::vec3(0.0f),
            glm::vec3(0.95f, 0.95f, 0.95f),  // Silver
            1.0f,   // Full metallic
            0.01f   // Very smooth
        );

        // Red Matte Sphere - Should NOT reflect
        CreatePBRActor(
            "RedSphere",
            "sphere",
            glm::vec3(2.0f, 0.5f, 0.0f),
            glm::vec3(1.0f),
            glm::vec3(0.0f),
            glm::vec3(0.8f, 0.2f, 0.2f),  // Red
            0.0f,   // Non-metallic
            0.9f    // Rough
        );

        // Blue Cube - Matte
        CreatePBRActor(
            "BlueCube",
            "cube",
            glm::vec3(0.0f, 0.5f, -2.0f),
            glm::vec3(1.0f),
            glm::vec3(0.0f, 45.0f, 0.0f),  // Rotated 45 degrees
            glm::vec3(0.2f, 0.3f, 0.8f),   // Blue
            0.0f,   // Non-metallic
            0.7f    // Somewhat rough
        );

        // Gold Cube - Metallic
        CreatePBRActor(
            "GoldCube",
            "cube",
            glm::vec3(0.0f, 0.5f, 2.0f),
            glm::vec3(1.0f),
            glm::vec3(0.0f, 30.0f, 0.0f),  // Rotated 30 degrees
            glm::vec3(1.0f, 0.84f, 0.0f),  // Gold color
            1.0f,   // Full metallic
            0.2f    // Slightly rough (brushed metal look)
        );

        // Green Cube - Slightly elevated
        CreatePBRActor(
            "GreenCube",
            "cube",
            glm::vec3(-3.0f, 1.0f, -1.0f),
            glm::vec3(0.7f),
            glm::vec3(15.0f, 20.0f, 0.0f),
            glm::vec3(0.2f, 0.7f, 0.3f),   // Green
            0.0f,   // Non-metallic
            0.5f    // Medium roughness
        );

        std::cout << "Created RT reflection test scene:" << std::endl;
        std::cout << "- Mirror floor (metallic=0.9, roughness=0.05)" << std::endl;
        std::cout << "- Mirror sphere at (-2, 0.5, 0)" << std::endl;
        std::cout << "- Red matte sphere at (2, 0.5, 0)" << std::endl;
        std::cout << "- Blue cube at (0, 0.5, -2)" << std::endl;
        std::cout << "- Gold cube at (0, 0.5, 2)" << std::endl;
        std::cout << "- Green cube at (-3, 1, -1)" << std::endl;
    }
};
