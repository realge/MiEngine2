#version 460
#extension GL_EXT_ray_tracing : require

// ============================================================================
// Descriptor Bindings (All in Set 0)
// ============================================================================

// Binding 3: Camera and settings (needed for IBL flag)
layout(set = 0, binding = 3) uniform RTUniforms {
    mat4 viewInverse;
    mat4 projInverse;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
    int frameNumber;
    int samplesPerPixel;
    int maxBounces;
    float reflectionBias;
    float shadowBias;
    float shadowSoftness;
    int flags;          // Bit 0: reflections, Bit 1: shadows, Bit 2: IBL enabled
    int debugMode;
} uniforms;

// Binding 4: Environment map
layout(set = 0, binding = 4) uniform samplerCube environmentMap;

// ============================================================================
// Ray Payload
// ============================================================================

layout(location = 0) rayPayloadInEXT struct RayPayload {
    vec3 color;
    float hitT;
    vec3 normal;
    float metallic;
    vec3 albedo;    // Base color for tinting reflections
    float roughness;
    int hit;
} payload;

// ============================================================================
// Main
// ============================================================================

void main() {
    // Sample environment map in the ray direction
    vec3 direction = gl_WorldRayDirectionEXT;

    // Check if IBL is enabled (bit 2 of flags)
    bool useIBL = (uniforms.flags & 4) != 0;

    if (useIBL) {
        // Sample the environment/skybox
        vec3 envColor = texture(environmentMap, direction).rgb;

        // Check if environment map is valid (not black/default)
        float envBrightness = dot(envColor, vec3(0.299, 0.587, 0.114));

        if (envBrightness > 0.001) {
            payload.color = envColor;
        } else {
            // Fallback gradient sky when environment map is invalid
            useIBL = false;
        }
    }

    if (!useIBL) {
        // Fallback gradient sky when IBL is disabled
        float t = direction.y * 0.5 + 0.5;  // Map y from [-1,1] to [0,1]
        vec3 skyColor = vec3(0.5, 0.7, 1.0);    // Light blue
        vec3 horizonColor = vec3(0.8, 0.85, 0.9); // Pale horizon
        vec3 groundColor = vec3(0.3, 0.25, 0.2);  // Brown ground

        if (direction.y > 0.0) {
            payload.color = mix(horizonColor, skyColor, t);
        } else {
            payload.color = mix(horizonColor, groundColor, -direction.y);
        }
    }

    payload.hitT = -1.0;
    payload.normal = vec3(0.0);
    payload.metallic = 0.0;
    payload.albedo = vec3(1.0);  // White (no tint for sky)
    payload.roughness = 1.0;
    payload.hit = 0;  // Miss
}
