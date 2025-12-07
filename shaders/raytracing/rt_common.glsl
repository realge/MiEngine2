// rt_common.glsl - Shared utilities for ray tracing shaders

#ifndef RT_COMMON_GLSL
#define RT_COMMON_GLSL

const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;
const float INV_PI = 0.31830988618;
const float EPSILON = 0.0001;

// ============================================================================
// Random Number Generation (PCG)
// ============================================================================

uint pcg_hash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Initialize RNG state from pixel coordinates and frame number
uint initRNG(uvec2 pixel, uint frame) {
    return pcg_hash(pixel.x + pixel.y * 65536u + frame * 16777216u);
}

// Generate random float in [0, 1)
float randomFloat(inout uint state) {
    state = pcg_hash(state);
    return float(state) / 4294967296.0;
}

// Generate random vec2 in [0, 1)^2
vec2 randomVec2(inout uint state) {
    return vec2(randomFloat(state), randomFloat(state));
}

// ============================================================================
// Sampling Functions
// ============================================================================

// Cosine-weighted hemisphere sampling
vec3 sampleHemisphereCosine(vec3 normal, inout uint rngState) {
    vec2 u = randomVec2(rngState);

    float r = sqrt(u.x);
    float phi = TWO_PI * u.y;

    vec3 H;
    H.x = r * cos(phi);
    H.y = r * sin(phi);
    H.z = sqrt(max(0.0, 1.0 - u.x));

    // Create orthonormal basis
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    return normalize(tangent * H.x + bitangent * H.y + normal * H.z);
}

// Uniform hemisphere sampling
vec3 sampleHemisphereUniform(vec3 normal, inout uint rngState) {
    vec2 u = randomVec2(rngState);

    float z = u.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = TWO_PI * u.y;

    vec3 H = vec3(r * cos(phi), r * sin(phi), z);

    // Create orthonormal basis
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    return normalize(tangent * H.x + bitangent * H.y + normal * H.z);
}

// Sample direction within a cone (for soft shadows)
vec3 sampleCone(vec3 direction, float cosThetaMax, inout uint rngState) {
    vec2 u = randomVec2(rngState);

    float cosTheta = 1.0 - u.x * (1.0 - cosThetaMax);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = TWO_PI * u.y;

    vec3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;

    // Create orthonormal basis
    vec3 up = abs(direction.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, direction));
    vec3 bitangent = cross(direction, tangent);

    return normalize(tangent * H.x + bitangent * H.y + direction * H.z);
}

// GGX importance sampling - generates half-vector H according to GGX distribution
// V = view direction (toward camera), N = surface normal, roughness = surface roughness [0,1]
// Returns reflection direction L (not half-vector)
vec3 sampleGGXReflection(vec3 V, vec3 N, float roughness, inout uint rngState) {
    vec2 u = randomVec2(rngState);

    // Use linear roughness directly for sampling (not squared)
    // This gives a more intuitive mapping where roughness 0.5 = 50% blur
    float a = max(roughness, 0.001);
    float a2 = a * a;

    // GGX importance sampling for half-vector H
    // cosTheta follows GGX distribution
    float cosTheta = sqrt((1.0 - u.x) / (1.0 + (a2 - 1.0) * u.x));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = TWO_PI * u.y;

    // Half-vector in tangent space (Z = N)
    vec3 H_tangent = vec3(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta
    );

    // Build orthonormal basis from normal
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = normalize(cross(N, tangent));

    // Transform H to world space
    vec3 H = normalize(tangent * H_tangent.x + bitangent * H_tangent.y + N * H_tangent.z);

    // Compute reflection direction
    vec3 L = reflect(-V, H);

    return L;
}

// ============================================================================
// PBR Functions
// ============================================================================

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX/Trowbridge-Reitz normal distribution
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Schlick-GGX geometry function
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's geometry function
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// ============================================================================
// Utility Functions
// ============================================================================

// Reconstruct world position from depth
vec3 reconstructWorldPosition(vec2 uv, float depth, mat4 invView, mat4 invProj) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = invProj * clipPos;
    viewPos /= viewPos.w;
    vec4 worldPos = invView * viewPos;
    return worldPos.xyz;
}

// Convert linear depth to view space Z
float linearizeDepth(float depth, float near, float far) {
    return near * far / (far - depth * (far - near));
}

// Encode normal to octahedral representation
vec2 encodeNormal(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return n.xy * 0.5 + 0.5;
}

// Decode normal from octahedral representation
vec3 decodeNormal(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f.xy, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

#endif // RT_COMMON_GLSL
