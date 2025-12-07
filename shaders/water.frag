#version 450

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragViewDir;
layout(location = 4) in float fragHeight;

// Output color
layout(location = 0) out vec4 outColor;

// Water uniform buffer (Set 0)
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

// Normal map (for additional detail sampling if needed)
layout(set = 0, binding = 2) uniform sampler2D normalMap;

// IBL textures (Set 1) - reusing existing IBL system
layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube prefilterMap;
layout(set = 1, binding = 2) uniform sampler2D brdfLUT;

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness for IBL
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // Normalize interpolated values
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(fragViewDir);

    // Calculate reflection direction
    vec3 R = reflect(-V, N);
    // Don't flip Y - let's match PBR shader behavior

    // Calculate view angle for Fresnel
    float NdotV = max(dot(N, V), 0.0);

    // Water has low base reflectivity (F0 ~ 0.02 for water)
    vec3 F0 = vec3(0.02);

    // Water roughness (relatively smooth but not perfectly mirror-like)
    float waterRoughness = 0.1;

    // Fresnel effect - more reflection at grazing angles
    // Using Schlick approximation with artistic control via fresnelPower
    float fresnelStrength = pow(clamp(1.0 - NdotV, 0.0, 1.0), ubo.fresnelPower);
    vec3 fresnel = mix(F0, vec3(1.0), fresnelStrength);

    // Sample IBL reflection
    // Calculate mip level based on roughness
    const float MAX_REFLECTION_LOD = 4.0; // Typical for prefiltered environment maps
    float mipLevel = waterRoughness * MAX_REFLECTION_LOD;
    vec3 prefilteredColor = textureLod(prefilterMap, R, mipLevel).rgb;

    // For water, directly use the reflection color
    // The Fresnel term will handle how much reflection we see
    vec3 reflectionColor = prefilteredColor * ubo.reflectionStrength;

    // Simple directional light for additional specular highlight
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 lightColor = vec3(1.0, 0.95, 0.9);
    vec3 H = normalize(V + lightDir);
    float specular = pow(max(dot(N, H), 0.0), ubo.specularPower) * 0.5;
    vec3 directSpecular = specular * lightColor;

    // Base water color with depth-based blend
    // Normalized height for color blending (0 = no wave, 1 = max wave)
    float normalizedHeight = (fragHeight / ubo.heightScale + 1.0) * 0.5;
    normalizedHeight = clamp(normalizedHeight, 0.0, 1.0);

    // Blend between shallow and deep color based on wave height
    vec3 waterColor = mix(ubo.deepColor.rgb, ubo.shallowColor.rgb, normalizedHeight);

    // Apply simple diffuse lighting to water color
    float diffuse = max(dot(N, lightDir), 0.0) * 0.5 + 0.5;
    vec3 litWater = waterColor * diffuse * lightColor;

    // Add subtle ambient from irradiance map
    vec3 irradiance = texture(irradianceMap, N).rgb;
    litWater += waterColor * irradiance * 0.1;

    // Blend reflection with water color based on Fresnel
    // Higher fresnel = more reflection, lower = more water color
    vec3 finalColor = mix(litWater, reflectionColor, fresnel);

    // Add direct specular highlight on top
    finalColor += directSpecular;

    // Add subtle rim lighting at edges for better visibility
    float rim = 1.0 - NdotV;
    rim = pow(rim, 3.0) * 0.2;
    finalColor += rim * prefilteredColor * 0.3;

    // Tone mapping (simple Reinhard)
    finalColor = finalColor / (finalColor + vec3(1.0));

    // Gamma correction
    finalColor = pow(finalColor, vec3(1.0/2.2));

    // Output with Fresnel-based transparency
    // Looking straight down (NdotV high, fresnelStrength low) = slightly transparent (see through to bottom)
    // Grazing angles (NdotV low, fresnelStrength high) = more opaque (see reflection)
    float alpha = 0.7 + fresnelStrength * 0.3;

    outColor = vec4(finalColor, alpha);
}
