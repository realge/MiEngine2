#version 450

// Fragment shader for copying depth buffer to Hi-Z mip 0
// This is used because sampling depth in compute shaders can be problematic
// A graphics pass with fragment shader is more reliable for depth sampling

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outDepth;

// Depth buffer input as a sampler2D (depth-only format)
layout(set = 0, binding = 0) uniform sampler2D depthTexture;

void main() {
    // Sample depth value - depth formats return depth in .r component
    float depth = texture(depthTexture, inUV).r;

    // Output raw depth value (no debug modifications)
    outDepth = depth;
}
