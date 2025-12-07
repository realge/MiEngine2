#version 460
#extension GL_EXT_ray_tracing : require

// ============================================================================
// Shadow Ray Payload
// ============================================================================

layout(location = 1) rayPayloadInEXT struct ShadowPayload {
    float visibility;  // 0 = in shadow, 1 = lit
} payload;

// ============================================================================
// Main
// ============================================================================

void main() {
    // Shadow ray missed all geometry - point is lit
    payload.visibility = 1.0;
}
