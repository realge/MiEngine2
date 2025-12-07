#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_samplerless_texture_functions : enable

const float PI = 3.14159265359;
const int MAX_LIGHTS = 16;
const int MAX_SHADOW_POINT_LIGHTS = 8;

struct Light {
    vec4 position;
    vec4 color;
    float radius;
    float falloff;
};

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPosition;
layout(location = 4) in mat3 TBN;
layout(location = 7) in vec3 fragViewDir;
layout(location = 8) in vec4 fragPosLightSpace;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;           // offset 0   (64 bytes)
    mat4 view;            // offset 64  (64 bytes)
    mat4 proj;            // offset 128 (64 bytes)
    vec4 cameraPos;       // offset 192 (16 bytes) - vec4 to match C++ layout
    float time;           // offset 208 (4 bytes)
    float maxReflectionLod; // offset 212 (4 bytes)
    vec2 _padding;        // offset 216 (8 bytes - pad to 224 for mat4 alignment)
    mat4 lightSpaceMatrix; // offset 224 (64 bytes)
} ubo;

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D emissiveMap;
layout(set = 1, binding = 4) uniform sampler2D occlusionMap;



layout(push_constant) uniform PushConstants {
    layout(offset = 0) mat4 model;
    layout(offset = 64) vec4 baseColorFactor;
    layout(offset = 80) float metallicFactor;
    layout(offset = 84) float roughnessFactor;
    layout(offset = 88) float ambientOcclusion;
    layout(offset = 92) float emissiveFactor;
    layout(offset = 96) int hasAlbedoMap;
    layout(offset = 100) int hasNormalMap;
    layout(offset = 104) int hasMetallicRoughnessMap;
    layout(offset = 108) int hasEmissiveMap;
    layout(offset = 112) int hasOcclusionMap;
    layout(offset = 116) int debugLayer;
    layout(offset = 120) int useIBL;
    layout(offset = 124) float iblIntensity;
    layout(offset = 128) int useRT;           // 0=off, 1=on (ray traced reflections/shadows)
    layout(offset = 132) float rtBlendFactor; // Blend factor for RT reflections (0-1)
    layout(offset = 136) int useRTReflections; // 0=off, 1=on (RT reflections specifically)
    layout(offset = 140) int useRTShadows;     // 0=off, 1=on (RT shadows specifically)
} pushConstants;

layout(set = 2, binding = 1) uniform sampler2D shadowMap;

// Point light shadow cubemap array
layout(set = 2, binding = 2) uniform samplerCubeArray pointLightShadowMaps;

// Point light shadow info buffer
struct PointLightShadowInfo {
    vec4 positionAndFarPlane;  // xyz = position, w = far plane
};

layout(set = 2, binding = 3) uniform PointLightShadowBuffer {
    PointLightShadowInfo shadowLights[MAX_SHADOW_POINT_LIGHTS];
    int shadowLightCount;
} pointLightShadows;

layout(set = 2, binding = 0) uniform LightBuffer {
    Light lights[MAX_LIGHTS];
    vec4 ambientColor;
    int lightCount;
} lightBuffer;

layout(set = 3, binding = 0) uniform samplerCube irradianceMap;
layout(set = 3, binding = 1) uniform samplerCube prefilterMap;
layout(set = 3, binding = 2) uniform sampler2D brdfLUT;

// Ray Tracing outputs (optional - only used when RT is enabled)
// Set 5 to avoid conflict with skeletal bone matrices (Set 4)
layout(set = 5, binding = 0) uniform sampler2D rtReflections;
layout(set = 5, binding = 1) uniform sampler2D rtShadows;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Shadow Calculation Function
float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    // Perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform XY to [0,1] range for texture sampling
    // In Vulkan, NDC X and Y are [-1,1], so we need this transformation
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // NOTE: In Vulkan, NDC Z is already [0,1], so we do NOT transform Z!
    // (Unlike OpenGL where Z is [-1,1])

    // Return no shadow when outside the light's frustum
    if(projCoords.z > 1.0 || projCoords.z < 0.0)
        return 0.0;
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;

    // Get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;

    // Calculate bias (based on depth map resolution and slope)
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);

    // PCF (Percentage-Closer Filtering) for softer edges
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow;
}

// Point Light Shadow Calculation using cubemap
float PointLightShadowCalculation(vec3 fragPos, int shadowIndex) {
    if (shadowIndex < 0 || shadowIndex >= pointLightShadows.shadowLightCount) {
        return 0.0;  // No shadow
    }

    vec3 lightPos = pointLightShadows.shadowLights[shadowIndex].positionAndFarPlane.xyz;
    float farPlane = pointLightShadows.shadowLights[shadowIndex].positionAndFarPlane.w;

    // Vector from light to fragment
    vec3 fragToLight = fragPos - lightPos;
    float currentDepth = length(fragToLight);

    // Normalize to far plane
    float normalizedDepth = currentDepth / farPlane;

    // Sample cubemap (direction determines which face)
    float closestDepth = texture(pointLightShadowMaps, vec4(fragToLight, float(shadowIndex))).r;

    // Bias to prevent shadow acne
    float bias = 0.0001;

    // PCF for softer shadows (sample neighboring texels)
    float shadow = 0.0;
    float samples = 4.0;
    float offset = 0.01;

    for(float x = -offset; x < offset; x += offset / (samples * 0.5)) {
        for(float y = -offset; y < offset; y += offset / (samples * 0.5)) {
            for(float z = -offset; z < offset; z += offset / (samples * 0.5)) {
                float pcfDepth = texture(pointLightShadowMaps,
                    vec4(fragToLight + vec3(x, y, z), float(shadowIndex))).r;
                shadow += normalizedDepth - bias > pcfDepth ? 1.0 : 0.0;
            }
        }
    }
    shadow /= (samples * samples * samples);

    // Fade shadow at edge of light range
    float fadeStart = 0.1;
    float fadeFactor = clamp((normalizedDepth - fadeStart) / (1.0 - fadeStart), 0.0, 1.0);
    shadow = mix(shadow, 0.0, fadeFactor);

    return shadow;
}

// Find shadow index for a point light by matching position
int FindPointLightShadowIndex(vec3 lightPos) {
    for (int i = 0; i < pointLightShadows.shadowLightCount; i++) {
        vec3 shadowLightPos = pointLightShadows.shadowLights[i].positionAndFarPlane.xyz;
        if (distance(lightPos, shadowLightPos) < 0.1) {
            return i;
        }
    }
    return -1;  // No shadow for this light
}


void main() {
    // Get material properties
    vec4 albedo = pushConstants.baseColorFactor;
    if (pushConstants.hasAlbedoMap > 0) {
        albedo = texture(albedoMap, fragTexCoord) * pushConstants.baseColorFactor;
    }
    albedo.rgb *= fragColor;
    
    float metallic = clamp(pushConstants.metallicFactor, 0.0, 1.0);
    float roughness = clamp(pushConstants.roughnessFactor, 0.001, 1.0);
    float ao = pushConstants.ambientOcclusion;
    
    if (pushConstants.hasMetallicRoughnessMap > 0) {
        vec4 metallicRoughness = texture(metallicRoughnessMap, fragTexCoord);
        roughness = clamp(metallicRoughness.g * roughness, 0.001, 1.0);
        metallic = clamp(metallicRoughness.b * metallic, 0.0, 1.0);
    }
    
    if (pushConstants.hasOcclusionMap > 0) {
        ao = texture(occlusionMap, fragTexCoord).r;
    }
    
    // Calculate vectors
    vec3 N = normalize(fragNormal);
    if (pushConstants.hasNormalMap > 0) {
        vec3 tangentNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }
    
    vec3 V = normalize(ubo.cameraPos.xyz - fragPosition);
    vec3 R = reflect(-V, N);
    
    //R.y = abs(R.y);
    // Calculate F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo.rgb, metallic);
    
    // Direct lighting
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < lightBuffer.lightCount; i++) {
        Light light = lightBuffer.lights[i];
        vec3 L;
        float attenuation = 1.0;
        
        if (light.position.w < 0.5) {
            // Directional light: position.xyz stores direction light travels (toward scene)
            // Negate to get L pointing toward light source
            L = normalize(-light.position.xyz);
        } else {
            vec3 lightVec = light.position.xyz - fragPosition;
            float distance = length(lightVec);
            L = normalize(lightVec);
            
            if (light.radius > 0.0) {
                float falloff = clamp(1.0 - (distance / light.radius), 0.0, 1.0);
                attenuation = falloff * falloff;
            }
        }
        
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        
        if (NdotL > 0.0) {
            float NDF = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
            
            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - metallic;
            
            vec3 numerator = NDF * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
            vec3 specular = numerator / denominator;
			
            // Calculate Shadow
            float shadow = 0.0;
            if (pushConstants.useRTShadows > 0 && light.position.w < 0.5 && i == 0) {
                // Use ray traced shadows for directional light
                vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(rtShadows, 0));
                float rtShadow = texture(rtShadows, screenUV).r;
                shadow = 1.0 - rtShadow; // RT outputs visibility (1=lit), we need shadow (1=shadow)
            } else if (light.position.w < 0.5) {
                // Directional light shadow (assuming light 0 is the directional sun)
                if (i == 0) {
                    shadow = ShadowCalculation(fragPosLightSpace, N, L);
                }
            } else {
                // Point light shadow
                int shadowIndex = FindPointLightShadowIndex(light.position.xyz);
                if (shadowIndex >= 0) {
                    shadow = PointLightShadowCalculation(fragPosition, shadowIndex);
                }
            }
			
            
            vec3 radiance = light.color.rgb * light.color.a * attenuation;
            
            // Apply Shadow (1.0 - shadow)
            Lo += (1.0 - shadow) * (kD * albedo.rgb / PI + specular) * radiance * NdotL; 
        }
    }
    
    // IBL
    vec3 ambient = vec3(0.0); // Default to black if no IBL/Lights
    
    // Variables for debug visualization
    vec3 diffuse = vec3(0.0);
    vec3 specular = vec3(0.0);
    vec3 prefilteredColor = vec3(0.0);
    vec2 envBRDF = vec2(0.0);
    float NdotV = clamp(dot(N, V), 0.0, 0.99);
    vec3 kD = vec3(0.0);

    if (pushConstants.useIBL > 0) {
        vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kS = F;
        kD = 1.0 - kS;
        kD *= 1.0 - metallic;

        // Diffuse IBL from irradiance map
        vec3 irradiance = texture(irradianceMap, N).rgb;
        diffuse = irradiance * albedo.rgb;

        // Calculate proper mip level based on roughness (perceptual mapping)
        const float MAX_REFLECTION_LOD = float(textureQueryLevels(prefilterMap) - 1);

        float mipLevel = roughness * MAX_REFLECTION_LOD;

        prefilteredColor = textureLod(prefilterMap, R, mipLevel).rgb;
        // Sample BRDF LUT with correct coordinates (match generation range)
        envBRDF = texture(brdfLUT, vec2(NdotV, roughness)).rg; // R=scale, G=bias

        // Apply split-sum approximation
        specular = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

        // Replace with ray traced reflections if enabled
        if (pushConstants.useRTReflections > 0 && metallic > 0.1) {
            vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(rtReflections, 0));
            vec3 rtReflection = texture(rtReflections, screenUV).rgb;
            // Fully replace IBL specular with RT reflections for metallic surfaces
            // This prevents the "transparency" look where IBL shows through dark RT areas
            specular = rtReflection;
        }

        ambient = (kD * diffuse + specular) * ao;
        ambient *= pushConstants.iblIntensity;
    } else {
        // Fallback ambient when IBL is disabled - use simple hemisphere lighting
        // This prevents complete darkness when only direct lighting is blocked by shadows
        vec3 skyColor = vec3(0.3, 0.4, 0.5);     // Blue-ish sky
        vec3 groundColor = vec3(0.15, 0.12, 0.1); // Brown-ish ground
        float hemisphereBlend = N.y * 0.5 + 0.5;
        vec3 hemisphereAmbient = mix(groundColor, skyColor, hemisphereBlend);

        // Simple Fresnel for non-IBL ambient
        vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        kD = (1.0 - F) * (1.0 - metallic);

        // Diffuse ambient
        vec3 diffuseAmbient = kD * hemisphereAmbient * albedo.rgb * ao * 0.3;

        // Specular/reflections - use RT if available, otherwise use hemisphere reflection
        vec3 specularAmbient = vec3(0.0);
        if (pushConstants.useRTReflections > 0 && metallic > 0.1) {
            // Use ray traced reflections
            vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(rtReflections, 0));
            vec3 rtReflection = texture(rtReflections, screenUV).rgb;
            specularAmbient = rtReflection * metallic;
        } else if (metallic > 0.1) {
            // Fallback: use hemisphere color in reflection direction
            float reflectY = reflect(-V, N).y;
            float reflectBlend = reflectY * 0.5 + 0.5;
            vec3 reflectColor = mix(groundColor, skyColor, reflectBlend);
            specularAmbient = reflectColor * F * 0.5;
        }

        ambient = diffuseAmbient + specularAmbient;
    }
    
    // Add emissive
    vec3 emissive = vec3(0.0);
    if (pushConstants.hasEmissiveMap > 0) {
        emissive = texture(emissiveMap, fragTexCoord).rgb * pushConstants.emissiveFactor;
    }
    
    // DEBUG: Isolate layers based on debugLayer value
    vec3 color = vec3(0.0);

    if (pushConstants.debugLayer == 1) {
        // Layer 1: Direct lighting only
        color = Lo;
    } else if (pushConstants.debugLayer == 2) {
        // Layer 2: Diffuse IBL only
        color = kD * diffuse * ao;
    } else if (pushConstants.debugLayer == 3) {
        // Layer 3: Specular IBL only
        color = specular * ao;
    } else if (pushConstants.debugLayer == 4) {
        // Layer 4: BRDF LUT visualization (R channel = scale)
        color = vec3(envBRDF.x);
    } else if (pushConstants.debugLayer == 5) {
        // Layer 5: BRDF LUT visualization (G channel = bias)
        color = vec3(envBRDF.y);
    } else if (pushConstants.debugLayer == 6) {
        // Layer 6: Prefiltered environment only
        color = prefilteredColor;
    } else if (pushConstants.debugLayer == 7) {
        // Layer 7: Full ambient (diffuse + specular IBL)
        color = ambient;
    } else if (pushConstants.debugLayer == 8) {
        // Layer 8: Irradiance map only (raw)
        color = texture(irradianceMap, N).rgb;
    } else if (pushConstants.debugLayer == 9) {
        // Layer 9: NdotV visualization
        color = vec3(NdotV);
    } else if (pushConstants.debugLayer == 10) {
        // Layer 10: Roughness visualization
        color = vec3(roughness);
    } else if (pushConstants.debugLayer == 11) {
        // Layer 11: BRDF LUT texture coordinates visualization
        // Red = NdotV, Green = roughness
        color = vec3(NdotV, roughness, 0.0);
    } else if (pushConstants.debugLayer == 12) {
        // Layer 12: Shadow map debug - shows shadow value
        // Black = in shadow, White = lit
        Light light = lightBuffer.lights[0];
        vec3 L = normalize(-light.position.xyz);
        float shadow = ShadowCalculation(fragPosLightSpace, N, L);
        color = vec3(1.0 - shadow); // White = lit, Black = shadow
    } else if (pushConstants.debugLayer == 13) {
        // Layer 13: Shadow map depth visualization
        // Sample the shadow map directly to see what's stored
        vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
        projCoords.xy = projCoords.xy * 0.5 + 0.5;
        float shadowDepth = texture(shadowMap, projCoords.xy).r;
        color = vec3(shadowDepth); // Visualize shadow map depth
    } else if (pushConstants.debugLayer == 14) {
        // Layer 14: Light space Z coordinate visualization
        vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
        color = vec3(projCoords.z); // Visualize fragment depth in light space
    } else if (pushConstants.debugLayer == 15) {
        // Layer 15: RT Shadow visualization (raw RT shadow output)
        // White = lit (1.0), Black = in shadow (0.0)
        if (pushConstants.useRTShadows > 0) {
            vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(rtShadows, 0));
            float rtShadow = texture(rtShadows, screenUV).r;
            color = vec3(rtShadow); // Directly show visibility value
        } else {
            color = vec3(0.5); // Gray = RT shadows disabled
        }
    } else if (pushConstants.debugLayer == 16) {
        // Layer 16: Vertex color visualization (for Virtual Geo cluster debug)
        color = fragColor;
    } else if (pushConstants.debugLayer == 17) {
        // Layer 17: Albedo only (after vertex color multiplication)
        color = albedo.rgb;
    } else {
        // Layer 0 or default: Complete PBR (all layers)
        color = ambient + Lo + emissive;
    }

    // Tone mapping and gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, albedo.a);
}