#pragma once
#include "core/Game.h"
#include "core/Input.h"
#include "scene/Scene.h"
#include "physics/RigidBodyComponent.h"
#include <iostream>

class PhysicsTestGame : public Game {
public:
    void OnInit() override {
        std::cout << "=== Physics Test Mode ===" << std::endl;
        std::cout << "Controls:" << std::endl;
        std::cout << "  SPACE - Apply upward impulse (jump)" << std::endl;
        std::cout << "  R     - Reset position" << std::endl;
        std::cout << "=========================" << std::endl;

        if (m_Scene) {
            m_Scene->setupDefaultLighting();

            MaterialTexturePaths texturePaths;

            // Load model - starts at Y=5, will fall due to gravity
            m_Scene->loadPBRModel(
                "models/blackrat.fbx",
                texturePaths,
                glm::vec3(0.0f, 5.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(1.0f)
            );

            // Enable physics on first mesh - it will fall!
            m_Scene->enablePhysics(static_cast<size_t>(0), RigidBodyType::Dynamic);

            auto* obj = m_Scene->getMeshInstance(0);
            if (obj && obj->rigidBody) {
                obj->rigidBody->mass = 1.0f;
                obj->rigidBody->linearDamping = 0.02f;
                std::cout << "Physics enabled - object will fall due to gravity!" << std::endl;
            }
        }

        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(0.0f, 2.0f, 10.0f));
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            m_Camera->setFarPlane(100.0f);
            m_Camera->setFOV(45.0f);
        }
    }

    void OnUpdate(float deltaTime) override {
        auto* obj = m_Scene->getMeshInstance(0);
        if (!obj || !obj->rigidBody) return;

        // SPACE: Jump (using Input system)
        if (Input::IsKeyPressed(GLFW_KEY_SPACE)) {
            if (!m_SpacePressed) {
                obj->rigidBody->addImpulse(glm::vec3(0.0f, 10.0f, 0.0f));
                std::cout << "Jump!" << std::endl;
                m_SpacePressed = true;
            }
        } else {
            m_SpacePressed = false;
        }

        // R: Reset position
        if (Input::IsKeyPressed(GLFW_KEY_R)) {
            if (!m_RPressed) {
                obj->transform.position = glm::vec3(0.0f, 5.0f, 0.0f);
                obj->rigidBody->velocity = glm::vec3(0.0f);
                std::cout << "Reset!" << std::endl;
                m_RPressed = true;
            }
        } else {
            m_RPressed = false;
        }

        // Print position every second
        m_DebugTimer += deltaTime;
        if (m_DebugTimer > 0.5f) {
            std::cout << "Y: " << obj->transform.position.y
                      << " | VelY: " << obj->rigidBody->velocity.y
                      << " | AccY: " << obj->rigidBody->acceleration.y
                      << " | Mass: " << obj->rigidBody->mass
                      << " | InvMass: " << obj->rigidBody->inverseMass
                      << std::endl;
            m_DebugTimer = 0.0f;
        }
    }

    void OnRender() override {}

    void OnShutdown() override {
        std::cout << "Physics Test Mode Shutdown" << std::endl;
    }

    // Disable default camera input so we can use SPACE and R for physics controls
    bool UsesDefaultCameraInput() const override { return false; }

private:
    bool m_SpacePressed = false;
    bool m_RPressed = false;
    float m_DebugTimer = 0.0f;
};
