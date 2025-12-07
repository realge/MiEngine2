#include "loader/ModelLoader.h"
#include <fbxsdk.h>
#include <iostream>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/ext/quaternion_geometric.hpp>

ModelLoader::ModelLoader() {
    // Initialize the FBX Manager
    fbxManager = FbxManager::Create();
    if (!fbxManager) {
        throw std::runtime_error("Error: Unable to create FBX Manager!");
    }

    // Create an IOSettings object.
    FbxIOSettings* ios = FbxIOSettings::Create(fbxManager, IOSROOT);
    fbxManager->SetIOSettings(ios);

    // Create the FBX scene.
    fbxScene = FbxScene::Create(fbxManager, "MyScene");
    if (!fbxScene) {
        throw std::runtime_error("Error: Unable to create FBX Scene!");
    }
  
}

ModelLoader::~ModelLoader() {
    // Clean up the FBX objects
    if (fbxScene) {
        fbxScene->Destroy();
    }
    if (fbxManager) {
        fbxManager->Destroy();
    }
}

bool ModelLoader::LoadModel(const std::string& filename) {
    // Create an importer using the FBX SDK.
    FbxImporter* importer = FbxImporter::Create(fbxManager, "");
    if (!importer->Initialize(filename.c_str(), -1, fbxManager->GetIOSettings())) {
        std::cerr << "Failed to initialize FBX importer: " 
                  << importer->GetStatus().GetErrorString() << std::endl;
        importer->Destroy();
        return false;
    }

    // Import the scene from the file.
    if (!importer->Import(fbxScene)) {
        std::cerr << "Failed to import FBX file." << std::endl;
        importer->Destroy();
        return false;
    }

    // Convert FBX units to meters (FBX files often use centimeters)
    FbxSystemUnit sceneSystemUnit = fbxScene->GetGlobalSettings().GetSystemUnit();
    if (sceneSystemUnit != FbxSystemUnit::m) {
        FbxSystemUnit::m.ConvertScene(fbxScene);
        std::cout << "Converted scene from " << sceneSystemUnit.GetScaleFactorAsString()
                  << " to meters" << std::endl;
    }

    FbxGeometryConverter geometryConverter(fbxManager);
    geometryConverter.Triangulate(fbxScene, true);
    importer->Destroy();

    // Process the scene starting from the root node.
    FbxNode* rootNode = fbxScene->GetRootNode();
    if (rootNode) {
        for (int i = 0; i < rootNode->GetChildCount(); i++) {
            ProcessNode(rootNode->GetChild(i), 0);
        }
    }
    for (auto& mesh : meshes) {
        CalculateTangents(mesh);
    }
    std::cout << "Total meshes loaded: " << meshes.size() << std::endl;
    return true;
}

void ModelLoader::ProcessNode(FbxNode* node, int indentLevel) {
    if (!node) return;

    // Calculate global transform of the node
    FbxAMatrix globalTransform = node->EvaluateGlobalTransform();

    // If the node contains a mesh, process it
    FbxMesh* fbxMesh = node->GetMesh();
    if (fbxMesh) {
        ProcessMesh(fbxMesh, globalTransform);
    }

    // Recursively process all children
    for (int i = 0; i < node->GetChildCount(); i++) {
        ProcessNode(node->GetChild(i), indentLevel + 1);
    }
}

void ModelLoader::ProcessMesh(FbxMesh* mesh, const FbxAMatrix& transform) {
    std::cout << "Starting mesh processing..." << std::endl;
    MeshData meshData;
    
    // Get vertex positions
    FbxVector4* controlPoints = mesh->GetControlPoints();
    
    // Prepare temporary storage for processed vertices
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Process each polygon
    int polygonCount = mesh->GetPolygonCount();
    for (int polygonIndex = 0; polygonIndex < polygonCount; polygonIndex++) {
        // FBX polygons should be triangulated, so each polygon should have 3 vertices
        if (mesh->GetPolygonSize(polygonIndex) != 3) {
            std::cerr << "Warning: Non-triangulated polygon found" << std::endl;
            continue;
        }
        
        // IMPORTANT: Process vertices in REVERSE order to flip winding
        int vertexOrder[3] = {2, 1, 0};
        
        for (int i = 0; i < 3; i++) {
            int vertexIndex = vertexOrder[i];
            Vertex vertex{};
            int controlPointIndex = mesh->GetPolygonVertex(polygonIndex, vertexIndex);
            
            // Get position
            FbxVector4 position = controlPoints[controlPointIndex];
            
            // Apply global transform to position
            FbxVector4 transformedPosition = transform.MultT(position);
            
            vertex.position = glm::vec3(
                static_cast<float>(transformedPosition[0]),
                static_cast<float>(transformedPosition[1]),
                static_cast<float>(transformedPosition[2])
            );
            
            // Get UV if available
            if (mesh->GetElementUV(0)) {
                FbxVector2 uv;
                bool unmapped;
                mesh->GetPolygonVertexUV(polygonIndex, vertexIndex, mesh->GetElementUV(0)->GetName(), uv, unmapped);
    
                // Check for invalid UV coordinates
                if (unmapped) {
                    vertex.texCoord = glm::vec2(0.5f, 0.5f); // Default to center
                } else {
                    // Clamp UV coordinates to valid range
                    float u = static_cast<float>(uv[0]);
                    float v = static_cast<float>(uv[1]);
        
                    // Flip V coordinate for Vulkan and clamp to [0,1]
                    vertex.texCoord = glm::vec2(
                        glm::clamp(u, 0.0f, 1.0f),
                        glm::clamp(1.0f - v, 0.0f, 1.0f)
                    );
                }
            } else {
                vertex.texCoord = glm::vec2(0.5f, 0.5f);
            }
            
            // Get normal
            if (mesh->GetElementNormal(0)) {
                FbxVector4 normal;
                mesh->GetPolygonVertexNormal(polygonIndex, vertexIndex, normal);

                // Apply global transform to normal (inverse transpose for correct normal transformation)
                // For orthogonal matrices (rotation/translation), just rotation part is enough
                FbxAMatrix rotationMatrix = transform;
                rotationMatrix.SetT(FbxVector4(0, 0, 0)); // Remove translation
                rotationMatrix.SetS(FbxVector4(1, 1, 1)); // Remove scaling (handle separately if non-uniform)

                FbxVector4 transformedNormal = rotationMatrix.MultT(normal);

                // Keep normals as-is (winding reversal handles face orientation)
                vertex.normal = glm::normalize(glm::vec3(
                    static_cast<float>(transformedNormal[0]),
                    static_cast<float>(transformedNormal[1]),
                    static_cast<float>(transformedNormal[2])
                ));
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            // Set default color
            vertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
            vertex.tangent = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            
            // Add vertex and index
            vertices.push_back(vertex);
            indices.push_back(static_cast<uint32_t>(vertices.size() - 1));
        }
    }
    
    meshData.vertices = vertices;
    meshData.indices = indices;
    meshes.push_back(meshData);
    
    std::cout << "Finished processing mesh. Added " 
              << vertices.size() << " vertices and "
              << indices.size() << " indices" << std::endl;
}



void ModelLoader::CalculateTangents(MeshData& meshData) {
    // Ensure the mesh has vertex positions, UVs, and indices
    if (meshData.vertices.empty() || meshData.indices.empty()) {
        return;
    }
    
    // Initialize tangents to zero
    for (auto& vertex : meshData.vertices) {
        vertex.tangent = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    // Process triangles
    for (size_t i = 0; i < meshData.indices.size(); i += 3) {
        // Get indices of vertices in this face
        uint32_t i0 = meshData.indices[i];
        uint32_t i1 = meshData.indices[i + 1];
        uint32_t i2 = meshData.indices[i + 2];
        
        // Get vertices
        Vertex& v0 = meshData.vertices[i0];
        Vertex& v1 = meshData.vertices[i1];
        Vertex& v2 = meshData.vertices[i2];
        
        // Get positions
        glm::vec3 pos0 = v0.position;
        glm::vec3 pos1 = v1.position;
        glm::vec3 pos2 = v2.position;
        
        // Get texture coordinates
        glm::vec2 uv0 = v0.texCoord;
        glm::vec2 uv1 = v1.texCoord;
        glm::vec2 uv2 = v2.texCoord;
        
        // Calculate edges
        glm::vec3 edge1 = pos1 - pos0;
        glm::vec3 edge2 = pos2 - pos0;
        
        // Calculate UV deltas
        glm::vec2 deltaUV1 = uv1 - uv0;
        glm::vec2 deltaUV2 = uv2 - uv0;
        
        // Calculate tangent
        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        
        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        
        // Add to all vertices in this face
        for (int j = 0; j < 3; j++) {
            uint32_t idx = meshData.indices[i + j];
            Vertex& v = meshData.vertices[idx];
            
            v.tangent.x += tangent.x;
            v.tangent.y += tangent.y;
            v.tangent.z += tangent.z;
        }
    }
    
    // Normalize and orthogonalize tangents
    for (auto& vertex : meshData.vertices) {
        // Get normal and tangent as glm vectors
        glm::vec3 n = vertex.normal;
        glm::vec3 t = glm::vec3(vertex.tangent);
        
        // Gram-Schmidt orthogonalize
        t = glm::normalize(t - n * glm::dot(n, t));
        
        // Calculate handedness (store in w component)
        glm::vec3 b = glm::cross(n, t);
        float handedness = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;
        
        // Store normalized tangent with handedness
        vertex.tangent = glm::vec4(t, handedness);
    }
}
MeshData ModelLoader::CreateSphere(float radius, int slices, int stacks) {
    MeshData meshData;
    
    // Generate vertices
    for (int stack = 0; stack <= stacks; stack++) {
        float phi = 3.14159265359f * (float)stack / (float)stacks;
        float sinPhi = sin(phi);
        float cosPhi = cos(phi);
        
        for (int slice = 0; slice <= slices; slice++) {
            float theta = 2.0f * 3.14159265359f * (float)slice / (float)slices;
            float sinTheta = sin(theta);
            float cosTheta = cos(theta);
            
            // Position
            float x = cosTheta * sinPhi;
            float y = cosPhi;
            float z = sinTheta * sinPhi;
            
            // Normal (normalized position for a sphere)
            glm::vec3 normal(x, y, z);
            
            // Calculate tangent properly
            glm::vec3 tangent;
            
            // Special handling for poles
            if (stack == 0 || stack == stacks) {
                // At poles, tangent is arbitrary but consistent
                tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            } else {
                // Standard tangent calculation
                tangent = glm::normalize(glm::vec3(-sinTheta, 0.0f, cosTheta));
            }
            
            // UV coordinates - standard spherical mapping
            float u = (float)slice / (float)slices;
            float v = (float)stack / (float)stacks;
            
            // Add vertex
            Vertex vertex;
            vertex.position = glm::vec3(x, y, z) * radius;
            vertex.normal = normal;
            vertex.texCoord = glm::vec2(u, v);
            vertex.color = glm::vec3(1.0f);
            vertex.tangent = glm::vec4(tangent, 1.0f);
            
            meshData.vertices.push_back(vertex);
        }
    }
    
    // Generate indices
    for (int stack = 0; stack < stacks; stack++) {
        for (int slice = 0; slice < slices; slice++) {
            int p1 = stack * (slices + 1) + slice;
            int p2 = p1 + (slices + 1);

            // First triangle (CCW for Vulkan: p1 -> p2 -> p1+1)
            meshData.indices.push_back(p1);
            meshData.indices.push_back(p2);
            meshData.indices.push_back(p1 + 1);

            // Second triangle (CCW for Vulkan: p1+1 -> p2 -> p2+1)
            meshData.indices.push_back(p1 + 1);
            meshData.indices.push_back(p2);
            meshData.indices.push_back(p2 + 1);
        }
    }
    
    return meshData;
}

MeshData ModelLoader::CreatePlane(float width, float height) {
    MeshData meshData;
    
    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    
    // Define vertices for a plane facing UP (positive Y)
    meshData.vertices = {
        // Position                      Color           Normal (up)      TexCoord    Tangent
        {{ -halfWidth, 0.0f,  halfHeight}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}, // Top-left
        {{  halfWidth, 0.0f,  halfHeight}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}, // Top-right  
        {{  halfWidth, 0.0f, -halfHeight}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}, // Bottom-right
        {{ -halfWidth, 0.0f, -halfHeight}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}  // Bottom-left
    };
    
    // Indices for two triangles, clockwise when viewed from above
    meshData.indices = {
        0, 2, 1,  // First triangle (clockwise from above: TL -> BR -> TR)
        0, 3, 2   // Second triangle (clockwise from above: TL -> BL -> BR)
    };
    
    return meshData;
}

MeshData ModelLoader::CreateCube(float size) {
    MeshData meshData;
    
    float halfSize = size / 2.0f;
    
    // Define the 8 vertices of the cube
    Vertex vertices[8];
    
    // Front face vertices (z = halfSize)
    vertices[0].position = glm::vec3(-halfSize, -halfSize, halfSize);  // Bottom left
    vertices[1].position = glm::vec3(halfSize, -halfSize, halfSize);   // Bottom right
    vertices[2].position = glm::vec3(halfSize, halfSize, halfSize);    // Top right
    vertices[3].position = glm::vec3(-halfSize, halfSize, halfSize);   // Top left
    
    // Back face vertices (z = -halfSize)
    vertices[4].position = glm::vec3(-halfSize, -halfSize, -halfSize); // Bottom left
    vertices[5].position = glm::vec3(halfSize, -halfSize, -halfSize);  // Bottom right
    vertices[6].position = glm::vec3(halfSize, halfSize, -halfSize);   // Top right
    vertices[7].position = glm::vec3(-halfSize, halfSize, -halfSize);  // Top left
    
    // Define the 6 faces (2 triangles per face = 12 triangles)
    // Face indices: [0, 1, 2] and [2, 3, 0] define a quad (face)
    uint32_t indices[36] = {
        // Front face (CW)
        0, 2, 1, 2, 0, 3,
        // Right face (CW)
        1, 6, 5, 6, 1, 2,
        // Back face (CW)
        5, 7, 4, 7, 5, 6,
        // Left face (CW)
        4, 3, 0, 3, 4, 7,
        // Top face (CW)
        3, 6, 2, 6, 3, 7,
        // Bottom face (CW)
        4, 1, 5, 1, 4, 0
    };
    
    // Define normals for each face
    glm::vec3 normals[6] = {
        glm::vec3(0.0f, 0.0f, 1.0f),   // Front
        glm::vec3(1.0f, 0.0f, 0.0f),   // Right
        glm::vec3(0.0f, 0.0f, -1.0f),  // Back
        glm::vec3(-1.0f, 0.0f, 0.0f),  // Left
        glm::vec3(0.0f, 1.0f, 0.0f),   // Top
        glm::vec3(0.0f, -1.0f, 0.0f)   // Bottom
    };
    
    // Define tangents for each face
    glm::vec4 tangents[6] = {
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),   // Front
        glm::vec4(0.0f, 0.0f, -1.0f, 1.0f),  // Right
        glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f),  // Back
        glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),   // Left
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),   // Top
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)    // Bottom
    };
    
    // Generate the vertices for each face
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            int idx = indices[i * 6 + j];
            
            Vertex vertex;
            vertex.position = vertices[idx].position;
            vertex.normal = normals[i];
            vertex.tangent = tangents[i];
            vertex.color = glm::vec3(1.0f);
            
            // Calculate texture coordinates based on face
            // This is a simple projection for each face
            glm::vec2 texCoord;
            if (i == 0) { // Front
                texCoord = glm::vec2(
                    0.5f + vertices[idx].position.x / size,
                    0.5f + vertices[idx].position.y / size
                );
            } else if (i == 1) { // Right
                texCoord = glm::vec2(
                    0.5f - vertices[idx].position.z / size,
                    0.5f + vertices[idx].position.y / size
                );
            } else if (i == 2) { // Back
                texCoord = glm::vec2(
                    0.5f - vertices[idx].position.x / size,
                    0.5f + vertices[idx].position.y / size
                );
            } else if (i == 3) { // Left
                texCoord = glm::vec2(
                    0.5f + vertices[idx].position.z / size,
                    0.5f + vertices[idx].position.y / size
                );
            } else if (i == 4) { // Top
                texCoord = glm::vec2(
                    0.5f + vertices[idx].position.x / size,
                    0.5f - vertices[idx].position.z / size
                );
            } else { // Bottom
                texCoord = glm::vec2(
                    0.5f + vertices[idx].position.x / size,
                    0.5f + vertices[idx].position.z / size
                );
            }
            
            vertex.texCoord = texCoord;
            meshData.vertices.push_back(vertex);
            meshData.indices.push_back(i * 6 + j);
        }
    }
    
    return meshData;
}