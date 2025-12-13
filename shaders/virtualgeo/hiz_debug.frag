#version 450

// Hi-Z Debug Visualization Fragment Shader

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hizPyramid;

layout(push_constant) uniform PushConstants {
    float mipLevel;
    float depthScale;
    uint visualizeMode;
    float padding;
} push;

void main() {
    // Flip Y for Vulkan coordinate system
    vec2 sampleUV = vec2(fragUV.x, 1.0 - fragUV.y);

    // Sample Hi-Z at the specified mip level
    float depth = textureLod(hizPyramid, sampleUV, push.mipLevel).r;

    // Mode 0: Show raw depth as grayscale (1.0 = white, 0.0 = black)
    // Mode 1: Show if depth is > 0.5 (white) or < 0.5 (black) - simple threshold test
    // Mode 2: Show UV gradient to verify sampling coordinates

    if (push.visualizeMode == 0) {
        // Raw grayscale - far plane (1.0) should be WHITE
        outColor = vec4(vec3(depth), 1.0);
    } else if (push.visualizeMode == 1) {
        // Threshold test: depth > 0.5 = white, depth < 0.5 = red
        if (depth > 0.5) {
            outColor = vec4(1.0, 1.0, 1.0, 1.0);  // White = depth > 0.5
        } else if (depth > 0.001) {
            outColor = vec4(1.0, 0.0, 0.0, 1.0);  // Red = 0.001 < depth < 0.5
        } else {
            outColor = vec4(0.0, 0.0, 1.0, 1.0);  // Blue = depth ~0
        }
    } else {
        // UV test pattern - ignore Hi-Z, just show UV coordinates
        // This verifies the fullscreen quad is rendering correctly
        outColor = vec4(sampleUV.x, sampleUV.y, 0.0, 1.0);
    }

    // Show actual depth value in top-left as colored bar
    // First 10% of screen width shows depth value
    if (fragUV.x < 0.1) {
        // Show depth as vertical bar (0 = bottom, 1 = top)
        if (fragUV.y < depth) {
            outColor = vec4(0.0, 1.0, 0.0, 1.0);  // Green bar showing depth level
        } else {
            outColor = vec4(0.2, 0.2, 0.2, 1.0);  // Dark gray background
        }
    }

    // Corner markers
    if (fragUV.x < 0.02 && fragUV.y < 0.02) {
        outColor = vec4(1.0, 1.0, 0.0, 1.0);  // Yellow top-left
    }
    if (fragUV.x > 0.98 && fragUV.y > 0.98) {
        outColor = vec4(0.0, 1.0, 1.0, 1.0);  // Cyan bottom-right
    }
}
