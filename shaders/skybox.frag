#version 450

layout(location = 0) in vec3 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube environmentMap;

void main() {
    vec3 envColor = texture(environmentMap, fragTexCoord).rgb;
    
    // Apply tone mapping (optional)
    envColor = envColor / (envColor + vec3(1.0));
    
    // Gamma correction
    envColor = pow(envColor, vec3(1.0/2.2));
    
    outColor = vec4(envColor, 1.0);
}