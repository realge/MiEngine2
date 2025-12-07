#include "debug/DebugUIManager.h"

#include "VulkanRenderer.h"
#include "camera/Camera.h"
#include "scene/Scene.h"
#include "asset/AssetBrowserWindow.h"
#include "asset/AssetImporter.h"
#include "include/debug/VirtualGeoDebugPanel.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "debug/DebugPanel.h"
#include <algorithm>
#include <cstring> 

//-----------------------------------------------------------------------------
// DebugUIManager Implementation
//-----------------------------------------------------------------------------

DebugUIManager::DebugUIManager(VulkanRenderer* renderer) 
    : renderer(renderer), device(VK_NULL_HANDLE), descriptorPool(VK_NULL_HANDLE),
      isVisible(true), initialized(false) {
}

DebugUIManager::~DebugUIManager() {
    if (initialized) {
        cleanup();
    }
}

void DebugUIManager::initialize(GLFWwindow* window, VkInstance instance, 
                                VkPhysicalDevice physicalDevice, VkDevice device,
                                uint32_t queueFamily, VkQueue queue, 
                                VkRenderPass renderPass, uint32_t imageCount) {
    this->device = device;
    
    // Create descriptor pool for ImGui
    createDescriptorPool(device);
    
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Setup style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.Alpha = 1.0f; // Solid background for premium feel

    // Premium Dark Theme Colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f); // Darker background
    colors[ImGuiCol_ChildBg]                = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.12f, 0.12f, 0.94f);
    colors[ImGuiCol_Border]                 = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); // Blue accent
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    
    // Initialize ImGui for GLFW and Vulkan
    ImGui_ImplGlfw_InitForVulkan(window, true);
    
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = queue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = descriptorPool;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = imageCount;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;
    init_info.PipelineInfoMain.RenderPass = renderPass;
    
    // Initialize ImGui Vulkan
    ImGui_ImplVulkan_Init(&init_info);
    
    initialized = true;
}

void DebugUIManager::createDescriptorPool(VkDevice device) {
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 }
    };
    
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 100 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool!");
    }
}

void DebugUIManager::cleanup() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }
    }
    
    initialized = false;
}

void DebugUIManager::beginFrame() {
    if (!initialized || !isVisible) return;
    
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Draw all active panels
    drawMainMenuBar();

    for (auto& panel : panels) {
        if (panel->getOpen()) {
            panel->draw();
        }
    }
}

void DebugUIManager::drawMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        // Scene menu (save/load)
        if (ImGui::BeginMenu("Scene")) {
            // Toggle Scene Manager panel
            for (auto& panel : panels) {
                if (panel->getName() == "Scene") {
                    bool isOpen = panel->getOpen();
                    if (ImGui::MenuItem("Scene Manager", "Ctrl+S", &isOpen)) {
                        panel->setOpen(isOpen);
                    }
                    break;
                }
            }
            ImGui::EndMenu();
        }

        // Assets menu
        if (ImGui::BeginMenu("Assets")) {
            if (ImGui::MenuItem("Asset Browser", "Ctrl+Shift+A")) {
                if (renderer && renderer->getAssetBrowser()) {
                    renderer->getAssetBrowser()->toggle();
                }
            }
            if (ImGui::MenuItem("Import Model...", "Ctrl+I")) {
                MiEngine::AssetImporter::showImportDialog();
            }
            ImGui::EndMenu();
        }

        // Virtual Geometry menu (cluster-based LOD system)
        if (ImGui::BeginMenu("Virtual Geometry")) {
            // Toggle Virtual Geometry Debug Panel
            for (auto& panel : panels) {
                if (panel->getName() == "Virtual Geometry") {
                    bool isOpen = panel->getOpen();
                    if (ImGui::MenuItem("Debug Panel", "Ctrl+G", &isOpen)) {
                        panel->setOpen(isOpen);
                    }
                    break;
                }
            }

            ImGui::Separator();

            // Virtual Geometry-specific options (placeholders for future features)
            ImGui::MenuItem("Enable Virtual Geometry", nullptr, false, false); // Disabled for now
            ImGui::MenuItem("Force LOD Level", nullptr, false, false);

            ImGui::Separator();

            // Get panel for visualization options
            auto vgPanel = getPanel<MiEngine::VirtualGeoDebugPanel>("Virtual Geometry");
            if (vgPanel) {
                bool showClusterColors = vgPanel->isClusterVisualizationEnabled();
                bool showLODColors = vgPanel->isLODVisualizationEnabled();
                bool wireframe = vgPanel->isWireframeEnabled();

                if (ImGui::MenuItem("Show Cluster Colors", nullptr, showClusterColors)) {
                    // Toggle handled in panel
                }
                if (ImGui::MenuItem("Show LOD Colors", nullptr, showLODColors)) {
                    // Toggle handled in panel
                }
                if (ImGui::MenuItem("Wireframe", nullptr, wireframe)) {
                    // Toggle handled in panel
                }
            } else {
                ImGui::MenuItem("Show Cluster Colors", nullptr, false, false);
                ImGui::MenuItem("Show LOD Colors", nullptr, false, false);
                ImGui::MenuItem("Wireframe", nullptr, false, false);
            }

            ImGui::EndMenu();
        }

        // View menu (debug panels)
        if (ImGui::BeginMenu("View")) {
            for (auto& panel : panels) {
                bool isOpen = panel->getOpen();
                if (ImGui::MenuItem(panel->getName().c_str(), nullptr, &isOpen)) {
                    panel->setOpen(isOpen);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void DebugUIManager::endFrame(VkCommandBuffer commandBuffer) {
    if (!initialized || !isVisible) return;
    
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);
}

void DebugUIManager::addPanel(std::shared_ptr<DebugPanel> panel) {
    panels.push_back(panel);
}

void DebugUIManager::removePanel(const std::string& name) {
    panels.erase(
        std::remove_if(panels.begin(), panels.end(),
            [&name](const std::shared_ptr<DebugPanel>& panel) {
                return panel->getName() == name;
            }),
        panels.end()
    );
}

void DebugUIManager::togglePanel(const std::string& name) {
    for (auto& panel : panels) {
        if (panel->getName() == name) {
            panel->toggle();
            break;
        }
    }
}


