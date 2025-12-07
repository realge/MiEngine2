// DebugUIManager.h
#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <functional>
#include <string>

// Forward declarations
class VulkanRenderer;
class DebugPanel;
class CameraDebugPanel;
class RenderDebugPanel;
class PerformancePanel;
class MaterialDebugPanel;
class Camera;
class Scene;
//-----------------------------------------------------------------------------
// Main Debug UI Manager - Handles ImGui initialization and panel management
//-----------------------------------------------------------------------------
class DebugUIManager {
public:
    DebugUIManager(VulkanRenderer* renderer);
    ~DebugUIManager();

    // Lifecycle
    void initialize(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                   VkDevice device, uint32_t queueFamily, VkQueue queue, 
                   VkRenderPass renderPass, uint32_t imageCount);
    void cleanup();
    
    // Frame operations
    void beginFrame();
    void endFrame(VkCommandBuffer commandBuffer);
    
    // Panel management
    void addPanel(std::shared_ptr<DebugPanel> panel);
    void removePanel(const std::string& name);
    void togglePanel(const std::string& name);
    
    // Visibility
    void setVisible(bool visible) { isVisible = visible; }
    bool getVisible() const { return isVisible; }
    void toggleVisibility() { isVisible = !isVisible; }
    
    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }

    void drawMainMenuBar();

    template<typename T>
std::shared_ptr<T> getPanel(const std::string& name) {
        for (auto& panel : panels) {
            if (panel->getName() == name) {
                return std::dynamic_pointer_cast<T>(panel);
            }
        }
        return nullptr;
    }

private:
    VulkanRenderer* renderer;
    VkDevice device;
    VkDescriptorPool descriptorPool;
    
    std::vector<std::shared_ptr<DebugPanel>> panels;
    bool isVisible;
    bool initialized;
    
    void createDescriptorPool(VkDevice device);
};