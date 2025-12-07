#version 450

// Vertex inputs
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

// Water uniform buffer
layout(set = 0, binding = 0) uniform WaterUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 shallowColor;
    vec4 deepColor;
    float time;
    float heightScale;
    float gridSize;
    float fresnelPower;
    float reflectionStrength;
    float specularPower;
    float padding1;
    float padding2;
} ubo;

// Height map for vertex displacement
layout(set = 0, binding = 1) uniform sampler2D heightMap;

// Normal map
layout(set = 0, binding = 2) uniform sampler2D normalMap;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragViewDir;
layout(location = 4) out float fragHeight;

void main() {
    // Sample height from height map
    float height = texture(heightMap, inTexCoord).r * ubo.heightScale;

    // Displace vertex position
    vec3 displacedPos = inPosition;
    displacedPos.y += height;

    // Transform to world space
    vec4 worldPos = ubo.model * vec4(displacedPos, 1.0);
    fragWorldPos = worldPos.xyz;

    // Sample and unpack normal from normal map
    vec3 packedNormal = texture(normalMap, inTexCoord).rgb;
    vec3 localNormal = packedNormal * 2.0 - 1.0;

    // Transform normal to world space
    mat3 normalMatrix = transpose(inverse(mat3(ubo.model)));
    fragNormal = normalize(normalMatrix * localNormal);

    // Calculate view direction
    fragViewDir = normalize(ubo.cameraPos.xyz - fragWorldPos);

    // Pass through texture coordinates
    fragTexCoord = inTexCoord;

    // Pass height for fragment shader effects
    fragHeight = height;

    // Final clip space position
    gl_Position = ubo.projection * ubo.view * worldPos;
}
