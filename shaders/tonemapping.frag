#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D inputImage;

layout(push_constant) uniform PushConstants {
    float exposure;
    float gamma;
    float saturation;
    float padding;
} push;

vec3 reinhardTonemap(vec3 color) {
    return color / (color + vec3(1.0));
}

vec3 acesTonemap(vec3 color) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 adjustSaturation(vec3 color, float saturation) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), color, saturation);
}

void main() {
    vec3 color = texture(inputImage, fragTexCoord).rgb;
    
    // Exposure
    color *= push.exposure;
    
    // Tonemap
    color = acesTonemap(color);
    
    // Saturation
    color = adjustSaturation(color, push.saturation);
    
    // Gamma correction
    color = pow(color, vec3(1.0 / push.gamma));
    
    outColor = vec4(color, 1.0);
}