#include "debug/RayTracingDebugPanel.h"
#include "VulkanRenderer.h"
#include "raytracing/RayTracingSystem.h"
#include "raytracing/RayTracingTypes.h"

RayTracingDebugPanel::RayTracingDebugPanel(VulkanRenderer* renderer)
    : DebugPanel("Ray Tracing", renderer) {
}

MiEngine::RayTracingSystem* RayTracingDebugPanel::getRTSystem() {
    return renderer->getRayTracingSystem();
}

void RayTracingDebugPanel::draw() {
    ImGui::SetNextWindowPos(ImVec2(10, 680), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 450), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(panelName.c_str(), &isOpen)) {
        drawStatusSection();

        MiEngine::RayTracingSystem* rtSystem = getRTSystem();
        if (rtSystem && rtSystem->isSupported()) {
            ImGui::Separator();
            drawRenderingSettings();
            ImGui::Separator();
            drawReflectionSettings();
            ImGui::Separator();
            drawShadowSettings();
            ImGui::Separator();
            drawDenoiserSettings();
            ImGui::Separator();
            drawDebugModes();
            ImGui::Separator();
            drawStatistics();
        }
    }
    ImGui::End();
}

void RayTracingDebugPanel::drawStatusSection() {
    MiEngine::RayTracingSystem* rtSystem = getRTSystem();

    ImGui::Text("Ray Tracing Status");
    ImGui::Indent();

    if (!rtSystem) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "RT System: Not Created");
        ImGui::Unindent();
        return;
    }

    const MiEngine::RTFeatureSupport& support = rtSystem->getFeatureSupport();

    if (!support.supported) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "RT Hardware: Not Supported");
        ImGui::TextWrapped("Reason: %s", support.unsupportedReason.c_str());
        ImGui::Unindent();
        return;
    }

    // Hardware support status
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "RT Hardware: Supported");

    // Feature checkmarks
    ImGui::Text("Features:");
    ImGui::SameLine();
    ImGui::TextColored(support.accelerationStructure ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                       "[AS]");
    ImGui::SameLine();
    ImGui::TextColored(support.rayTracingPipeline ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                       "[Pipeline]");
    ImGui::SameLine();
    ImGui::TextColored(support.rayQuery ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       "[RayQuery]");

    // System ready status
    if (rtSystem->isReady()) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "System: Ready");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "System: Initializing...");
    }

    ImGui::Unindent();
}

void RayTracingDebugPanel::drawRenderingSettings() {
    MiEngine::RayTracingSystem* rtSystem = getRTSystem();
    if (!rtSystem || !rtSystem->isReady()) return;

    MiEngine::RTSettings& settings = rtSystem->getSettings();

    ImGui::Text("Rendering");

    // Main enable toggle
    bool wasEnabled = settings.enabled;
    if (ImGui::Checkbox("Enable Ray Tracing", &settings.enabled)) {
        if (settings.enabled && !wasEnabled) {
            // Force TLAS rebuild when enabling
            rtSystem->markTLASDirty();
        }
    }

    if (!settings.enabled) {
        ImGui::TextDisabled("(RT disabled - using rasterization)");
        return;
    }

    // Samples per pixel
    ImGui::SliderInt("Samples Per Pixel", &settings.samplesPerPixel, 1, 16);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Higher = better quality, lower performance\n1-4 SPP for real-time, 8-16 for quality");
    }

    // Max bounces
    ImGui::SliderInt("Max Bounces", &settings.maxBounces, 1, 4);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Number of reflection bounces\n1-2 recommended for real-time");
    }
}

void RayTracingDebugPanel::drawReflectionSettings() {
    MiEngine::RayTracingSystem* rtSystem = getRTSystem();
    if (!rtSystem || !rtSystem->isReady()) return;

    MiEngine::RTSettings& settings = rtSystem->getSettings();
    if (!settings.enabled) return;

    if (ImGui::CollapsingHeader("Reflections", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable Reflections", &settings.enableReflections);

        if (settings.enableReflections) {
            ImGui::Indent();

            ImGui::SliderFloat("Reflection Bias", &settings.reflectionBias, 0.001f, 0.5f, "%.3f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Offset to prevent self-intersection artifacts");
            }

            ImGui::Unindent();
        }
    }
}

void RayTracingDebugPanel::drawShadowSettings() {
    MiEngine::RayTracingSystem* rtSystem = getRTSystem();
    if (!rtSystem || !rtSystem->isReady()) return;

    MiEngine::RTSettings& settings = rtSystem->getSettings();
    if (!settings.enabled) return;

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable Soft Shadows", &settings.enableSoftShadows);

        if (settings.enableSoftShadows) {
            ImGui::Indent();

            ImGui::SliderFloat("Shadow Bias", &settings.shadowBias, 0.001f, 0.5f, "%.3f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Offset to prevent self-shadowing artifacts\nIncrease if you see staircase patterns on curved surfaces");
            }

            ImGui::SliderFloat("Shadow Softness", &settings.shadowSoftness, 0.0f, 0.1f, "%.3f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Light source radius for soft shadow penumbra\n0 = hard shadows");
            }

            ImGui::Unindent();
        }
    }
}

void RayTracingDebugPanel::drawDenoiserSettings() {
    MiEngine::RayTracingSystem* rtSystem = getRTSystem();
    if (!rtSystem || !rtSystem->isReady()) return;

    MiEngine::RTSettings& settings = rtSystem->getSettings();
    if (!settings.enabled) return;

    MiEngine::DenoiserSettings& denoiser = rtSystem->getDenoiserSettings();

    if (ImGui::CollapsingHeader("Denoiser", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable Denoising", &settings.enableDenoising);

        if (settings.enableDenoising) {
            ImGui::Indent();

            // Temporal settings
            ImGui::Text("Temporal Accumulation");
            ImGui::Checkbox("Enable Temporal", &denoiser.enableTemporal);

            if (denoiser.enableTemporal) {
                ImGui::SliderFloat("Temporal Blend", &denoiser.temporalBlend, 0.01f, 0.5f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Lower = more history (smoother but more ghosting)\nHigher = more current frame (noisier but responsive)");
                }

                ImGui::SliderFloat("Variance Clip", &denoiser.varianceClipGamma, 0.5f, 3.0f, "%.1f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Strength of variance clipping to prevent ghosting\nHigher = more aggressive clipping");
                }
            }

            ImGui::Spacing();

            // Spatial settings
            ImGui::Text("Spatial Filter");
            ImGui::Checkbox("Enable Spatial", &denoiser.enableSpatial);

            if (denoiser.enableSpatial) {
                ImGui::SliderInt("Filter Radius", &denoiser.spatialFilterRadius, 1, 4);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Kernel radius for bilateral filter\nLarger = smoother but slower");
                }

                ImGui::SliderFloat("Color Sigma", &denoiser.spatialColorSigma, 0.1f, 2.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Color similarity weight\nLower = more edge-aware");
                }

                ImGui::SliderFloat("Spatial Sigma", &denoiser.spatialSigma, 0.5f, 5.0f, "%.1f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Spatial falloff weight");
                }
            }

            ImGui::Unindent();
        }
    }
}

void RayTracingDebugPanel::drawDebugModes() {
    MiEngine::RayTracingSystem* rtSystem = getRTSystem();
    if (!rtSystem || !rtSystem->isReady()) return;

    MiEngine::RTSettings& settings = rtSystem->getSettings();
    if (!settings.enabled) return;

    if (ImGui::CollapsingHeader("Debug Visualization")) {
        // Must match raygen.rgen debug switch cases!
        const char* debugModes[] = {
            "0: Off (Normal Rendering)",
            "1: Normals",
            "2: Depth (Hit Distance)",
            "3: Metallic",
            "4: Roughness",
            "5: Reflections Only",
            "6: Shadows Only"
        };
        const int numModes = sizeof(debugModes) / sizeof(debugModes[0]);

        int currentMode = settings.debugMode;
        if (currentMode < 0 || currentMode >= numModes) currentMode = 0;

        if (ImGui::BeginCombo("Debug Mode", debugModes[currentMode])) {
            for (int i = 0; i < numModes; i++) {
                if (ImGui::Selectable(debugModes[i], currentMode == i)) {
                    settings.debugMode = i;
                }
            }
            ImGui::EndCombo();
        }

        if (settings.debugMode > 0) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Debug mode active");
        }
    }
}

void RayTracingDebugPanel::drawStatistics() {
    MiEngine::RayTracingSystem* rtSystem = getRTSystem();
    if (!rtSystem || !rtSystem->isReady()) return;

    MiEngine::RTSettings& settings = rtSystem->getSettings();

    if (ImGui::CollapsingHeader("Statistics")) {
        ImGui::Text("Acceleration Structures:");
        ImGui::Indent();
        ImGui::Text("BLAS Count: %u", rtSystem->getBLASCount());
        ImGui::Text("TLAS Instances: %u", rtSystem->getTLASInstanceCount());
        ImGui::Unindent();

        ImGui::Spacing();

        // Pipeline properties
        const MiEngine::RTPipelineProperties& pipelineProps = rtSystem->getPipelineProperties();
        ImGui::Text("Pipeline Properties:");
        ImGui::Indent();
        ImGui::Text("Max Recursion Depth: %u", pipelineProps.maxRayRecursionDepth);
        ImGui::Text("Shader Group Handle Size: %u", pipelineProps.shaderGroupHandleSize);
        ImGui::Unindent();

        ImGui::Spacing();

        // Current settings summary
        ImGui::Text("Current Configuration:");
        ImGui::Indent();
        ImGui::Text("RT Enabled: %s", settings.enabled ? "Yes" : "No");
        if (settings.enabled) {
            ImGui::Text("SPP: %d, Bounces: %d", settings.samplesPerPixel, settings.maxBounces);
            ImGui::Text("Reflections: %s", settings.enableReflections ? "On" : "Off");
            ImGui::Text("Soft Shadows: %s", settings.enableSoftShadows ? "On" : "Off");
            ImGui::Text("Denoising: %s", settings.enableDenoising ? "On" : "Off");
        }
        ImGui::Unindent();
    }
}
