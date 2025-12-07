#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "core/Input.h"
#include "loader/ModelLoader.h"
#include <vector>
#include <random>
#include <iostream>

class FlappyBirdGame : public Game {
public:
    enum class GameState {
        Menu,
        Playing,
        GameOver
    };

    struct Bird {
        glm::vec3 position;
        float velocityY;
        float radius;
    };

    struct Pipe {
        glm::vec3 position;
        float gapSize;
        float width;
        bool active;
        bool passed;
        size_t topMeshIndex;
        size_t bottomMeshIndex;
    };

    void OnInit() override {
        std::cout << "FlappyBirdGame Initialized" << std::endl;
        
        // Setup Scene
        if (m_Scene) {
            m_Scene->setupDefaultLighting();
            // m_Scene->setupEnvironment("hdr/sky.hdr"); // Optional, might fail if file missing
            
            // Add a directional light
            m_Scene->addLight(
                glm::vec3(1.0f, 1.0f, 1.0f),
                glm::vec3(1.0f, 1.0f, 1.0f),
                1.0f, 0.0f, 1.0f, true
            );
        }

        // Setup Camera
        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(0.0f, 0.0f, 15.0f)); // Side view
            m_Camera->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            m_Camera->setFarPlane(1000.0f);
            m_Camera->setFOV(60.0f);
        }

        // Generate Meshes
        ModelLoader loader;
        m_BirdMesh = loader.CreateSphere(1.0f, 32, 32);
        m_PipeMesh = loader.CreateCube(1.0f);

        // Create Materials
        CreateMaterials();

        // Initialize Object Pool
        InitializePool();

        ResetGame();
    }

    void OnUpdate(float deltaTime) override {
        if (Input::IsKeyPressed(GLFW_KEY_R)) {
            ResetGame();
        }

        if (m_State == GameState::Menu) {
            if (Input::IsKeyPressed(GLFW_KEY_SPACE)) {
                m_State = GameState::Playing;
                m_Bird.velocityY = m_JumpStrength;
            }
        }
        else if (m_State == GameState::Playing) {
            // Bird Physics
            m_Bird.velocityY += m_Gravity * deltaTime;
            m_Bird.position.y += m_Bird.velocityY * deltaTime;

            if (Input::IsKeyPressed(GLFW_KEY_SPACE) && !m_SpacePressed) {
                m_Bird.velocityY = m_JumpStrength;
                m_SpacePressed = true;
            }
            if (!Input::IsKeyPressed(GLFW_KEY_SPACE)) {
                m_SpacePressed = false;
            }

            // Pipe Spawning
            m_PipeSpawnTimer += deltaTime;
            if (m_PipeSpawnTimer >= m_PipeSpawnInterval) {
                SpawnPipe();
                m_PipeSpawnTimer = 0.0f;
            }

            // Update Pipes
            for (auto& pipe : m_Pipes) {
                if (pipe.active) {
                    pipe.position.x -= m_PipeSpeed * deltaTime;
                    if (pipe.position.x < -20.0f) {
                        pipe.active = false;
                        // Move meshes away
                        UpdatePipeMeshes(pipe); 
                    }
                }
            }

            // Collision Detection
            CheckCollisions();
        }

        // Update Visuals
        UpdateScene();
    }

    void OnRender() override {
        // UI rendering would go here
    }

    void OnShutdown() override {
        std::cout << "FlappyBirdGame Shutdown" << std::endl;
    }

    bool UsesDefaultCameraInput() const override { return false; }

private:
    void CreateMaterials() {
        if (!m_Scene) return;

        // Bird Material (Yellow)
        // Use blackrat_color.png as a fallback texture since we know it exists
        m_BirdMaterial = m_Scene->createPBRMaterial(
            "texture/blackrat_color.png", "", "", "", "", "", 
            0.0f, 0.5f, glm::vec3(1.0f, 1.0f, 0.0f)
        );

        // Pipe Material (Green)
        m_PipeMaterial = m_Scene->createPBRMaterial(
            "texture/blackrat_color.png", "", "", "", "", "", 
            0.0f, 0.5f, glm::vec3(0.0f, 1.0f, 0.0f)
        );
    }

    void InitializePool() {
        if (!m_Scene) return;
        m_Scene->clearMeshInstances();

        // Create Bird Mesh Instance
        std::vector<MeshData> birdMeshes = { m_BirdMesh };
        Transform birdTransform;
        birdTransform.position = glm::vec3(-100.0f); // Hide initially
        m_Scene->createMeshesFromData(birdMeshes, birdTransform, std::make_shared<Material>(m_BirdMaterial));
        m_BirdMeshIndex = 0; // First mesh is bird

        // Create Pipe Pool (10 pairs)
        std::vector<MeshData> pipeMeshes = { m_PipeMesh };
        for (int i = 0; i < 10; ++i) {
            Pipe pipe;
            pipe.active = false;
            
            // Top Pipe Mesh
            Transform topTransform;
            topTransform.position = glm::vec3(-100.0f);
            m_Scene->createMeshesFromData(pipeMeshes, topTransform, std::make_shared<Material>(m_PipeMaterial));
            pipe.topMeshIndex = m_BirdMeshIndex + 1 + (i * 2);

            // Bottom Pipe Mesh
            Transform bottomTransform;
            bottomTransform.position = glm::vec3(-100.0f);
            m_Scene->createMeshesFromData(pipeMeshes, bottomTransform, std::make_shared<Material>(m_PipeMaterial));
            pipe.bottomMeshIndex = m_BirdMeshIndex + 1 + (i * 2) + 1;

            m_Pipes.push_back(pipe);
        }
    }

    void ResetGame() {
        m_State = GameState::Menu;
        m_Bird.position = glm::vec3(-5.0f, 0.0f, 0.0f);
        m_Bird.velocityY = 0.0f;
        m_Bird.radius = 0.5f;
        
        for (auto& pipe : m_Pipes) {
            pipe.active = false;
            UpdatePipeMeshes(pipe);
        }
        
        m_PipeSpawnTimer = 0.0f;
        m_SpacePressed = false;
        
        UpdateBirdMesh();
    }

    void SpawnPipe() {
        // Find inactive pipe
        for (auto& pipe : m_Pipes) {
            if (!pipe.active) {
                pipe.active = true;
                pipe.position = glm::vec3(15.0f, GetRandomHeight(), 0.0f);
                pipe.gapSize = 4.0f;
                pipe.width = 1.0f;
                pipe.passed = false;
                UpdatePipeMeshes(pipe);
                return;
            }
        }
    }

    float GetRandomHeight() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<> dis(-3.0f, 3.0f);
        return static_cast<float>(dis(gen));
    }

    void CheckCollisions() {
        // Ground/Ceiling collision
        if (m_Bird.position.y < -8.0f || m_Bird.position.y > 8.0f) {
            m_State = GameState::GameOver;
            std::cout << "Game Over: Hit Ground/Ceiling" << std::endl;
        }

        // Pipe collision
        for (const auto& pipe : m_Pipes) {
            if (!pipe.active) continue;

            // AABB collision check (simplified)
            bool inXRange = (m_Bird.position.x + m_Bird.radius > pipe.position.x - pipe.width &&
                             m_Bird.position.x - m_Bird.radius < pipe.position.x + pipe.width);
            
            if (inXRange) {
                bool inGap = (m_Bird.position.y - m_Bird.radius > pipe.position.y - pipe.gapSize / 2.0f &&
                              m_Bird.position.y + m_Bird.radius < pipe.position.y + pipe.gapSize / 2.0f);
                
                if (!inGap) {
                    m_State = GameState::GameOver;
                    std::cout << "Game Over: Hit Pipe" << std::endl;
                }
            }
        }
    }

    void UpdateScene() {
        if (!m_Scene) return;
        UpdateBirdMesh();
        for (const auto& pipe : m_Pipes) {
            if (pipe.active) {
                UpdatePipeMeshes(pipe);
            }
        }
    }

    void UpdateBirdMesh() {
        auto* instance = m_Scene->getMeshInstance(m_BirdMeshIndex);
        if (instance) {
            instance->transform.position = m_Bird.position;
            instance->transform.scale = glm::vec3(m_Bird.radius);
        }
    }

    void UpdatePipeMeshes(const Pipe& pipe) {
        auto* topInstance = m_Scene->getMeshInstance(pipe.topMeshIndex);
        auto* bottomInstance = m_Scene->getMeshInstance(pipe.bottomMeshIndex);

        if (pipe.active) {
            if (topInstance) {
                topInstance->transform.position = glm::vec3(pipe.position.x, pipe.position.y + pipe.gapSize / 2.0f + 5.0f, 0.0f);
                topInstance->transform.scale = glm::vec3(pipe.width, 10.0f, 1.0f);
            }
            if (bottomInstance) {
                bottomInstance->transform.position = glm::vec3(pipe.position.x, pipe.position.y - pipe.gapSize / 2.0f - 5.0f, 0.0f);
                bottomInstance->transform.scale = glm::vec3(pipe.width, 10.0f, 1.0f);
            }
        } else {
            // Hide
            if (topInstance) topInstance->transform.position = glm::vec3(-100.0f);
            if (bottomInstance) bottomInstance->transform.position = glm::vec3(-100.0f);
        }
    }

    GameState m_State = GameState::Menu;
    Bird m_Bird;
    std::vector<Pipe> m_Pipes;
    
    MeshData m_BirdMesh;
    MeshData m_PipeMesh;
    
    Material m_BirdMaterial;
    Material m_PipeMaterial;
    
    size_t m_BirdMeshIndex = 0;
    
    float m_Gravity = -20.0f;
    float m_JumpStrength = 8.0f;
    float m_PipeSpeed = 5.0f;
    float m_PipeSpawnTimer = 0.0f;
    float m_PipeSpawnInterval = 2.0f;
    bool m_SpacePressed = false;
};
