#version 450
#extension GL_ARB_separate_shader_objects : enable

// ============================================================================
// Virtual Geometry Cluster Fragment Shader
// ============================================================================

// Input from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

// Output
layout(location = 0) out vec4 outColor;

// Uniform buffer - camera and lighting (shared across all instances)
// Must match vertex shader UBO layout exactly
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 lightDirection;    // xyz = direction, w = intensity
    vec4 lightColor;        // rgb = color, a = ambient
} ubo;

// Push constants - per-instance data (must match vertex shader)
layout(push_constant) uniform PushConstants {
    mat4 model;             // 64 bytes
    uint debugMode;         // 0 = normal, 1 = cluster colors, 2 = normals, 3 = LOD
    uint lodLevel;
    uint clusterId;
    uint pad;
} push;

// Generate cluster color from ID
vec3 getClusterColor(uint id) {
    float hue = fract(float(id) * 0.618033988749895);
    float h = hue * 6.0;
    float c = 0.8;
    float x = c * (1.0 - abs(mod(h, 2.0) - 1.0));

    vec3 rgb;
    if (h < 1.0) rgb = vec3(c, x, 0.0);
    else if (h < 2.0) rgb = vec3(x, c, 0.0);
    else if (h < 3.0) rgb = vec3(0.0, c, x);
    else if (h < 4.0) rgb = vec3(0.0, x, c);
    else if (h < 5.0) rgb = vec3(x, 0.0, c);
    else rgb = vec3(c, 0.0, x);

    return rgb + 0.2;
}

// LOD level colors
vec3 getLodColor(uint lod) {
    vec3 lodColors[8] = vec3[8](
        vec3(0.0, 1.0, 0.0),   // LOD 0 - green (highest detail)
        vec3(0.5, 1.0, 0.0),   // LOD 1
        vec3(1.0, 1.0, 0.0),   // LOD 2 - yellow
        vec3(1.0, 0.5, 0.0),   // LOD 3
        vec3(1.0, 0.0, 0.0),   // LOD 4 - red
        vec3(1.0, 0.0, 0.5),   // LOD 5
        vec3(1.0, 0.0, 1.0),   // LOD 6
        vec3(0.5, 0.0, 1.0)    // LOD 7 (lowest detail)
    );
    return lodColors[min(lod, 7u)];
}

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 lightDir = normalize(-ubo.lightDirection.xyz);

    // Debug modes
    if (push.debugMode == 1) {
        // Cluster color visualization
        outColor = vec4(getClusterColor(push.clusterId), 1.0);
        return;
    }
    else if (push.debugMode == 2) {
        // Normal visualization
        outColor = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }
    else if (push.debugMode == 3) {
        // LOD level visualization
        outColor = vec4(getLodColor(push.lodLevel), 1.0);
        return;
    }

    // Normal shading
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * ubo.lightColor.rgb * ubo.lightDirection.w;

    vec3 ambient = ubo.lightColor.a * ubo.lightColor.rgb;

    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 32.0);
    vec3 specular = spec * ubo.lightColor.rgb * 0.3;

    vec3 baseColor = vec3(0.7, 0.7, 0.7);
    vec3 result = (ambient + diffuse) * baseColor + specular;

    outColor = vec4(result, 1.0);
}
