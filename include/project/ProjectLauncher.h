#pragma once

#include <string>
#include <functional>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

class ProjectManager;

// Result of the launcher dialog
enum class LauncherResult {
    None,           // Still showing dialog
    ProjectOpened,  // User opened/created a project
    Cancelled       // User cancelled (close window)
};

// Project launcher window - shown before main engine starts
class ProjectLauncher {
public:
    ProjectLauncher();
    ~ProjectLauncher();

    // Run the launcher and return when user makes a choice
    LauncherResult run();

    // Get error message if something failed
    const std::string& getErrorMessage() const { return m_ErrorMessage; }

    // Get selected game mode (1-10)
    int getSelectedGameMode() const { return m_SelectedGameMode; }

private:
    // Window setup
    bool initWindow();
    bool initVulkan();
    bool initImGui();
    void cleanup();

    // UI Drawing
    void drawFrame();
    void drawLauncherUI();
    void drawProjectList();
    void drawNewProjectDialog();
    void drawOpenProjectDialog();

    // File dialogs (Windows native)
    bool showFolderBrowserDialog(std::string& outPath);
    bool showOpenFileDialog(std::string& outPath);

    // State
    GLFWwindow* m_Window = nullptr;
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    uint32_t m_QueueFamily = 0;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;

    std::vector<VkImage> m_SwapchainImages;
    std::vector<VkImageView> m_SwapchainImageViews;
    std::vector<VkFramebuffer> m_Framebuffers;
    std::vector<VkCommandBuffer> m_CommandBuffers;
    VkFormat m_SwapchainFormat;
    VkExtent2D m_SwapchainExtent;

    VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence m_InFlightFence = VK_NULL_HANDLE;

    // Dialog state
    LauncherResult m_Result = LauncherResult::None;
    bool m_ShowNewProjectDialog = false;
    bool m_ShowOpenProjectDialog = false;

    // New project form
    char m_NewProjectName[256] = "";
    char m_NewProjectPath[512] = "";
    char m_NewProjectAuthor[256] = "";
    char m_NewProjectDescription[1024] = "";

    std::string m_ErrorMessage;
    bool m_Initialized = false;

    // Game mode selection
    int m_SelectedGameMode = 2; // Default to Editor

    static const int WINDOW_WIDTH = 800;
    static const int WINDOW_HEIGHT = 600;
};
