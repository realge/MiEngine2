#version 450
#extension GL_ARB_separate_shader_objects : enable

// Constants
const float PI = 3.14159265359;

// Input from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPosition;  // Added
layout(location = 4) in mat3 TBN;           // Added (uses locations 4, 5, 6)
layout(location = 7) in vec3 fragViewDir; 


// Texture samplers - using your existing binding points
layout(set = 1, binding = 0) uniform sampler2D texSampler; // Diffuse/Albedo

// Output color
layout(location = 0) out vec4 outColor;

// Simple PBR parameters - hardcoded for now
// These would normally come from a uniform buffer
const float metallic = 0.0;      // 0.0 = non-metal, 1.0 = metal
const float roughness = 0.5;     // 0.0 = smooth, 1.0 = rough
const vec3 lightColor = vec3(1.0, 1.0, 1.0);
const vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
const vec3 viewDir = normalize(vec3(0.0, 0.0, 1.0)); // Simplified view direction

// PBR helper functions
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return nom / max(denom, 0.001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / max(denom, 0.001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

void main() {
    // Sample the diffuse texture
    vec4 albedo = texture(texSampler, fragTexCoord);
    albedo.rgb *= fragColor; // Apply vertex color
    
    // Normalize the normal
    vec3 N = normalize(fragNormal);
    vec3 L = lightDir;
    vec3 V = viewDir;
    vec3 H = normalize(V + L);
    
    // Calculate reflectance at normal incidence
    vec3 F0 = vec3(0.04); // Dielectric surface default F0
    F0 = mix(F0, albedo.rgb, metallic);
    
    // Cook-Torrance BRDF components
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    // Calculate specular component
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3 specular = numerator / denominator;
    
    // Energy conservation
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic; // No diffuse for metals
    
    // Calculate direct lighting contribution
    float NdotL = max(dot(N, L), 0.0);
    
    // Basic ambient approximation (would be replaced by IBL)
    
    float ambientStrength = 0.2;
    vec3 ambient = ambientStrength * albedo.rgb;
    
    // Combine lighting components
    vec3 result = (kD * albedo.rgb / PI + specular) * lightColor * NdotL + ambient;
    
    // Simple tone mapping and gamma correction
    result = result / (result + vec3(1.0)); // Reinhard tone mapping
    result = pow(result, vec3(1.0/2.2));    // Gamma correction
    
    outColor = vec4(result, albedo.a);
}