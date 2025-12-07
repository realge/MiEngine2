#pragma once
#include "core/Game.h"
#include "scene/Scene.h"
#include "core/Input.h"
#include "loader/ModelLoader.h"
#include <vector>
#include <array>
#include <random>
#include <iostream>
#include <set>
#include <utility>
#include <glm/gtc/noise.hpp>

class MinecraftGame : public Game {
public:
    enum class BlockType : uint8_t {
        Air = 0,
        Dirt,
        Grass,
        Stone,
        Bedrock
    };

    static constexpr int CHUNK_SIZE = 16;
    static constexpr int CHUNK_HEIGHT = 64;

    struct Chunk {
        glm::ivec3 position; // Position in chunk coordinates (x, y, z)
        BlockType blocks[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE];
        size_t meshIndex = -1;
        bool isDirty = true;
        
        // Fractal Noise Helper
        static float GetNoise(float x, float z, int octaves, float persistence, float lacunarity, int seed) {
            float total = 0.0f;
            float frequency = 1.0f;
            float amplitude = 1.0f;
            float maxValue = 0.0f;  // Used for normalizing result to 0.0 - 1.0

            for (int i = 0; i < octaves; i++) {
                // Offset seed for each octave to avoid artifacts
                float noiseVal = glm::perlin(glm::vec2(x * frequency + seed * 10.0f + i * 100.0f, z * frequency + seed * 10.0f + i * 100.0f));
                total += noiseVal * amplitude;

                maxValue += amplitude;

                amplitude *= persistence;
                frequency *= lacunarity;
            }

            return total / maxValue;
        }

        void GenerateTerrain(int seed) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    float worldX = static_cast<float>(position.x * CHUNK_SIZE + x);
                    float worldZ = static_cast<float>(position.z * CHUNK_SIZE + z);
                    
                    // Base terrain noise (large features)
                    float baseNoise = GetNoise(worldX * 0.01f, worldZ * 0.01f, 4, 0.5f, 2.0f, seed);
                    
                    // Mountain noise (where mountains should be)
                    float mountainNoise = GetNoise(worldX * 0.005f, worldZ * 0.005f, 2, 0.5f, 2.0f, seed + 100);
                    
                    // Detail noise (roughness)
                    float detailNoise = GetNoise(worldX * 0.05f, worldZ * 0.05f, 4, 0.5f, 2.0f, seed + 200);

                    // Combine noises
                    // Map baseNoise from [-1, 1] to [0, 1] roughly
                    float heightMap = (baseNoise + 1.0f) * 0.5f; 
                    
                    // Increase height based on mountain influence
                    float mountainInfluence = (mountainNoise + 1.0f) * 0.5f;
                    mountainInfluence = std::pow(mountainInfluence, 3.0f); // Sharpen mountains
                    
                    float finalHeight = heightMap * 20.0f + mountainInfluence * 40.0f + detailNoise * 2.0f;
                    
                    int height = static_cast<int>(finalHeight) + 10; // Base level 10
                    if (height >= CHUNK_HEIGHT) height = CHUNK_HEIGHT - 1;
                    if (height < 1) height = 1;

                    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                        if (y == 0) {
                            blocks[x][y][z] = BlockType::Bedrock;
                        } else if (y < height - 3) {
                            blocks[x][y][z] = BlockType::Stone;
                        } else if (y < height) {
                            blocks[x][y][z] = BlockType::Dirt;
                        } else if (y == height) {
                            // Snow on high peaks
                            if (y > 45) blocks[x][y][z] = BlockType::Stone; // Or Snow if we had it
                            else blocks[x][y][z] = BlockType::Grass;
                        } else {
                            blocks[x][y][z] = BlockType::Air;
                        }
                    }
                }
            }
        }
    };

    void OnInit() override {
        std::cout << "MinecraftGame Initialized" << std::endl;
        m_Scene->clearLights();
        if (m_Scene) {
            m_Scene->clearLights();
            m_Scene->addLight(
                glm::vec3(0.5f, -1.0f, 0.5f),  // Direction pointing DOWN to light the top
                glm::vec3(1.0f, 0.98f, 0.95f), // Warm white
                1.5f,                           // Higher intensity for visible shadows
                0.0f,
                1.0f,
                true                            // Directional light
            );
        }

        if (m_Camera) {
            m_Camera->setPosition(glm::vec3(0.0f, 40.0f, 0.0f));
            m_Camera->lookAt(glm::vec3(10.0f, 20.0f, 10.0f));
            m_Camera->setFarPlane(1000.0f);
            m_Camera->setFOV(60.0f);
        }

        CreateMaterials();
        
        // Initial chunk generation - generate ALL needed chunks
        UpdateChunks(-1);
    }

    void OnUpdate(float deltaTime) override {
        if (!m_Camera) return;

        // Check for new chunks based on player position
        // Limit to 1 chunk per frame to prevent stutter
        UpdateChunks(1);

        // Toggle Walk/Freecam
        static bool gPressed = false;
        if (Input::IsKeyPressed(GLFW_KEY_G)) {
            if (!gPressed) {
                m_IsWalkMode = !m_IsWalkMode;
                std::cout << "Switched to " << (m_IsWalkMode ? "Walk" : "Freecam") << " mode" << std::endl;
                m_Velocity = glm::vec3(0.0f); // Reset velocity
                gPressed = true;
            }
        } else {
            gPressed = false;
        }

        if (!m_IsWalkMode) {
            // Freecam Mode - Manual control since we disabled default movement
            float speedMultiplier = 1.0f;
            if (Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT)) speedMultiplier = 2.0f;

            if (Input::IsKeyPressed(GLFW_KEY_W)) m_Camera->processKeyboard(CameraMovement::FORWARD, deltaTime, speedMultiplier);
            if (Input::IsKeyPressed(GLFW_KEY_S)) m_Camera->processKeyboard(CameraMovement::BACKWARD, deltaTime, speedMultiplier);
            if (Input::IsKeyPressed(GLFW_KEY_A)) m_Camera->processKeyboard(CameraMovement::LEFT, deltaTime, speedMultiplier);
            if (Input::IsKeyPressed(GLFW_KEY_D)) m_Camera->processKeyboard(CameraMovement::RIGHT, deltaTime, speedMultiplier);
            if (Input::IsKeyPressed(GLFW_KEY_SPACE)) m_Camera->processKeyboard(CameraMovement::UP, deltaTime, speedMultiplier);
            if (Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) 
                m_Camera->processKeyboard(CameraMovement::DOWN, deltaTime, speedMultiplier);
        } else {
            // Walk Mode
            glm::vec3 front = m_Camera->getFront();
            glm::vec3 right = m_Camera->getRight();
            
            // Flatten vectors for movement (ignore Y)
            front.y = 0;
            right.y = 0;
            front = glm::normalize(front);
            right = glm::normalize(right);

            glm::vec3 moveDir(0.0f);
            if (Input::IsKeyPressed(GLFW_KEY_W)) moveDir += front;
            if (Input::IsKeyPressed(GLFW_KEY_S)) moveDir -= front;
            if (Input::IsKeyPressed(GLFW_KEY_A)) moveDir -= right;
            if (Input::IsKeyPressed(GLFW_KEY_D)) moveDir += right;

            if (glm::length(moveDir) > 0) {
                moveDir = glm::normalize(moveDir);
                glm::vec3 moveStep = moveDir * m_WalkSpeed * deltaTime;
                
                // Try moving in X
                glm::vec3 newPos = m_Camera->position;
                newPos.x += moveStep.x;
                // Check collision at head (eye level), waist, and feet
                // Camera position is at eye level, so we check:
                // - Eye level: newPos (head)
                // - Waist: newPos - 0.5 * height
                // - Feet: newPos - full height
                bool collisionX = CheckCollision(newPos) || 
                                  CheckCollision(newPos - glm::vec3(0, m_PlayerHeight * 0.5f, 0)) ||
                                  CheckCollision(newPos - glm::vec3(0, m_PlayerHeight * 0.5f, 0)) ||
                                  CheckCollision(newPos - glm::vec3(0, m_PlayerHeight - 0.1f, 0));
                
                if (!collisionX) {
                    m_Camera->position.x = newPos.x;
                }

                // Try moving in Z
                newPos = m_Camera->position;
                newPos.z += moveStep.z;
                bool collisionZ = CheckCollision(newPos) || 
                                  CheckCollision(newPos - glm::vec3(0, m_PlayerHeight * 0.5f, 0)) ||
                                  CheckCollision(newPos - glm::vec3(0, m_PlayerHeight * 0.5f, 0)) ||
                                  CheckCollision(newPos - glm::vec3(0, m_PlayerHeight - 0.1f, 0));

                if (!collisionZ) {
                    m_Camera->position.z = newPos.z;
                }
            }

            // Jump
            if (m_IsGrounded && Input::IsKeyPressed(GLFW_KEY_SPACE)) {
                m_Velocity.y = m_JumpForce;
                m_IsGrounded = false;
            }

            // Gravity
            m_Velocity.y += m_Gravity * deltaTime;

            // Apply Velocity
            m_Camera->position.y += m_Velocity.y * deltaTime;

            // Ground Collision
            // Check block BELOW feet (not at feet level, which would include walls)
            glm::vec3 footPos = m_Camera->position;
            footPos.y -= m_PlayerHeight; // Eye level is at position, feet are below
            
            // Check slightly below the feet (0.1 units below) to detect ground
            glm::vec3 belowFeetPos = footPos;
            belowFeetPos.y -= 0.1f;

            // Check center and 4 corners for ground
            bool isGrounded = false;
            float r = m_PlayerRadius;
            
            if (GetBlockAt(belowFeetPos) != BlockType::Air ||
                GetBlockAt(belowFeetPos + glm::vec3(r, 0, r)) != BlockType::Air ||
                GetBlockAt(belowFeetPos + glm::vec3(r, 0, -r)) != BlockType::Air ||
                GetBlockAt(belowFeetPos + glm::vec3(-r, 0, r)) != BlockType::Air ||
                GetBlockAt(belowFeetPos + glm::vec3(-r, 0, -r)) != BlockType::Air) {
                isGrounded = true;
            }

            if (isGrounded) {
                // Landed - snap to top of the block
                if (m_Velocity.y < 0) {
                    m_Camera->position.y = std::floor(belowFeetPos.y) + 1.0f + m_PlayerHeight;
                    m_Velocity.y = 0;
                    m_IsGrounded = true;
                }
            } else {
                m_IsGrounded = false;
            }
            
            // Hard floor at Y=-10 (Void)
            if (m_Camera->position.y < -10.0f) {
                 m_Camera->position.y = -10.0f;
                 m_Velocity.y = 0;
                 m_IsGrounded = true;
            }
        }
    }

    void OnRender() override {
        // UI rendering
    }

    void OnShutdown() override {
        std::cout << "MinecraftGame Shutdown" << std::endl;
    }

    bool UsesDefaultCameraInput() const override { return true; }
    bool UsesDefaultCameraMovement() const override { return false; }

private:
    bool m_IsWalkMode = false;
    glm::vec3 m_Velocity = glm::vec3(0.0f);
    bool m_IsGrounded = false;
    float m_Gravity = -20.0f;
    float m_JumpForce = 8.0f;
    float m_WalkSpeed = 5.0f;
    float m_PlayerHeight = 1.8f;
    float m_PlayerRadius = 0.3f;
    
    std::vector<Chunk> m_Chunks;
    std::set<std::pair<int, int>> m_LoadedChunkCoords;
    Material m_BlockMaterial;
    int m_ViewDistance = 8;
    int m_Seed = 12345;

    void UpdateChunks(int maxChunksToGenerate = -1) {
        if (!m_Camera) return;

        int playerChunkX = static_cast<int>(std::floor(m_Camera->position.x / CHUNK_SIZE));
        int playerChunkZ = static_cast<int>(std::floor(m_Camera->position.z / CHUNK_SIZE));

        int chunksGenerated = 0;

        for (int x = -m_ViewDistance; x <= m_ViewDistance; ++x) {
            for (int z = -m_ViewDistance; z <= m_ViewDistance; ++z) {
                int cx = playerChunkX + x;
                int cz = playerChunkZ + z;

                if (m_LoadedChunkCoords.find({cx, cz}) == m_LoadedChunkCoords.end()) {
                    // Check limit
                    if (maxChunksToGenerate != -1 && chunksGenerated >= maxChunksToGenerate) {
                        return; // Stop generating for this frame
                    }

                    // Generate new chunk
                    Chunk chunk;
                    chunk.position = glm::ivec3(cx, 0, cz);
                    chunk.GenerateTerrain(m_Seed);
                    m_Chunks.push_back(chunk);
                    m_LoadedChunkCoords.insert({cx, cz});
                    
                    // Generate mesh for the new chunk immediately
                    GenerateAndAddChunkMesh(m_Chunks.back());
                    chunksGenerated++;
                }
            }
        }
    }

    // Helper to get block at world position
    BlockType GetBlockAt(glm::vec3 pos) {
        int cx = static_cast<int>(std::floor(pos.x / CHUNK_SIZE));
        int cz = static_cast<int>(std::floor(pos.z / CHUNK_SIZE));
        
        // Find chunk
        // Optimization: Could use a map for faster lookup if needed, but vector is okay for now
        for (const auto& chunk : m_Chunks) {
            if (chunk.position.x == cx && chunk.position.z == cz) {
                int lx = static_cast<int>(std::floor(pos.x)) - cx * CHUNK_SIZE;
                int ly = static_cast<int>(std::floor(pos.y));
                int lz = static_cast<int>(std::floor(pos.z)) - cz * CHUNK_SIZE;

                if (lx >= 0 && lx < CHUNK_SIZE && ly >= 0 && ly < CHUNK_HEIGHT && lz >= 0 && lz < CHUNK_SIZE) {
                    return chunk.blocks[lx][ly][lz];
                }
            }
        }
        return BlockType::Air;
    }

    bool CheckCollision(glm::vec3 pos) {
        // Check center and 4 corners of the player's bounding box
        // This prevents clipping through corners or thin walls
        float r = m_PlayerRadius;
        
        // Center
        if (GetBlockAt(pos) != BlockType::Air) return true;
        
        // 4 Corners
        if (GetBlockAt(pos + glm::vec3(r, 0, r)) != BlockType::Air) return true;
        if (GetBlockAt(pos + glm::vec3(r, 0, -r)) != BlockType::Air) return true;
        if (GetBlockAt(pos + glm::vec3(-r, 0, r)) != BlockType::Air) return true;
        if (GetBlockAt(pos + glm::vec3(-r, 0, -r)) != BlockType::Air) return true;

        return false;
    }

    void ResolveCollision(glm::vec3& position, glm::vec3& velocity) {
        // ... (Vertical collision logic remains similar or can be integrated)
    }

    void CreateMaterials() {
        if (!m_Scene) return;
        // Using a texture atlas or simple colors for now. 
        // For simplicity, let's just use the existing blackrat texture but we'll map UVs to look okay-ish, 
        // or just rely on vertex colors if we had them. 
        // Use empty strings for textures to rely on vertex colors and default white texture
        m_BlockMaterial = m_Scene->createPBRMaterial(
            "", "", "", "", "", "", 
            0.0f, 1.0f, glm::vec3(1.0f)
        );
    }

    void GenerateAndAddChunkMesh(Chunk& chunk) {
        if (!m_Scene) return;

        if (chunk.isDirty) {
            MeshData meshData = GenerateMeshForChunk(chunk);
            
            if (!meshData.vertices.empty()) {
                Transform transform;
                transform.position = glm::vec3(chunk.position.x * CHUNK_SIZE, 0.0f, chunk.position.z * CHUNK_SIZE);
                
                std::vector<MeshData> meshes = { meshData };
                m_Scene->createMeshesFromData(meshes, transform, std::make_shared<Material>(m_BlockMaterial));
            }
            chunk.isDirty = false;
        }
    }

    MeshData GenerateMeshForChunk(const Chunk& chunk) {
        MeshData mesh;
        
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    BlockType type = chunk.blocks[x][y][z];
                    if (type == BlockType::Air) continue;

                    glm::vec3 pos(x, y, z);
                    
                    // Check neighbors
                    if (ShouldRenderFace(chunk, x, y, z + 1)) AddFace(mesh, pos, 0, type); // Front
                    if (ShouldRenderFace(chunk, x, y, z - 1)) AddFace(mesh, pos, 1, type); // Back
                    if (ShouldRenderFace(chunk, x + 1, y, z)) AddFace(mesh, pos, 2, type); // Right
                    if (ShouldRenderFace(chunk, x - 1, y, z)) AddFace(mesh, pos, 3, type); // Left
                    if (ShouldRenderFace(chunk, x, y + 1, z)) AddFace(mesh, pos, 4, type); // Top
                    if (ShouldRenderFace(chunk, x, y - 1, z)) AddFace(mesh, pos, 5, type); // Bottom
                }
            }
        }
        
        return mesh;
    }

    bool ShouldRenderFace(const Chunk& chunk, int x, int y, int z) {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_SIZE) {
            return true; // Draw face if it borders the chunk edge (simplification: assume neighbor chunk is air or we want to see edge)
        }
        return chunk.blocks[x][y][z] == BlockType::Air;
    }

    void AddFace(MeshData& mesh, glm::vec3 pos, int faceIndex, BlockType type) {
        // Face normals
        glm::vec3 normals[] = {
            {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}
        };
        
        // Proper tangents for each face (perpendicular to normal)
        glm::vec4 tangents[] = {
            {1, 0, 0, 1},   // Front (z+1): tangent points right
            {-1, 0, 0, 1},  // Back (z-1): tangent points left
            {0, 0, -1, 1},  // Right (x+1): tangent points back
            {0, 0, 1, 1},   // Left (x-1): tangent points forward
            {0, 0, 1, 1},   // Top (y+1): tangent points forward
            {0, 0, -1, 1}   // Bottom (y-1): tangent points back (changed to match coordinate flip)
        };

        // Vertices for a 1x1x1 cube centered at pos + 0.5
        // But here pos is corner, so let's say block is from pos to pos+1
        
        // 0: Front, 1: Back, 2: Right, 3: Left, 4: Top, 5: Bottom
        
        // Relative vertices for each face
        // Front (z+1) - Swapped 1&3 to flip (was CCW, now CW)
        float vFront[] = {
            0, 0, 1,  0, 1, 1,  1, 1, 1,  1, 0, 1
        };
        // Back (z) - CCW
        float vBack[] = {
            1, 0, 0,  1, 1, 0,  0, 1, 0,  0, 0, 0
        };
        // Right (x+1) - CCW
        float vRight[] = {
            1, 0, 1,  1, 1, 1,  1, 1, 0,  1, 0, 0
        };
        // Left (x) - Swapped 1&3 to flip (was CCW, now CW)
        float vLeft[] = {
            0, 0, 0,  0, 1, 0,  0, 1, 1,  0, 0, 1
        };
        // Top (y+1) - CCW
        float vTop[] = {
            0, 1, 1,  0, 1, 0,  1, 1, 0,  1, 1, 1
        };
        // Bottom (y) - Swapped 1&3 to flip winding
        float vBottom[] = {
            0, 0, 0,  0, 0, 1,  1, 0, 1,  1, 0, 0
        };

        float* v = nullptr;
        switch(faceIndex) {
            case 0: v = vFront; break;
            case 1: v = vBack; break;
            case 2: v = vRight; break;
            case 3: v = vLeft; break;
            case 4: v = vTop; break;
            case 5: v = vBottom; break;
        }

        uint32_t startIndex = static_cast<uint32_t>(mesh.vertices.size());
        
        // Block Colors
        glm::vec3 color(1.0f);
        
        // Debug: Make top face cyan to identify it
        if (faceIndex == 4) {
            color = glm::vec3(0.0f, 1.0f, 1.0f); // Cyan for top face
        } else {
            if (type == BlockType::Grass) color = glm::vec3(0.0f, 1.0f, 0.0f);
            else if (type == BlockType::Dirt) color = glm::vec3(0.5f, 0.35f, 0.05f);
            else if (type == BlockType::Stone) color = glm::vec3(0.5f, 0.5f, 0.5f);
            else if (type == BlockType::Bedrock) color = glm::vec3(0.1f, 0.1f, 0.1f);
        }

        for (int i = 0; i < 4; ++i) {
            Vertex vert;
            vert.position = glm::vec3(v[i*3], v[i*3+1], v[i*3+2]) + pos;
            vert.normal = normals[faceIndex];
            vert.color = color;
            vert.texCoord = glm::vec2(i < 2 ? 0 : 1, i % 2 == 0 ? 0 : 1); // Simple UVs
            vert.tangent = tangents[faceIndex]; // Proper tangent per face
            mesh.vertices.push_back(vert);
        }

        // Indices (0, 1, 2, 2, 3, 0)
        mesh.indices.push_back(startIndex + 0);
        mesh.indices.push_back(startIndex + 1);
        mesh.indices.push_back(startIndex + 2);
        mesh.indices.push_back(startIndex + 2);
        mesh.indices.push_back(startIndex + 3);
        mesh.indices.push_back(startIndex + 0);
    }
};
