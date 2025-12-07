#include "project/ProjectLauncher.h"
#include "project/ProjectManager.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

ProjectLauncher::ProjectLauncher() {
}

ProjectLauncher::~ProjectLauncher() {
    cleanup();
}

LauncherResult ProjectLauncher::run() {
    if (!initWindow()) {
        m_ErrorMessage = "Failed to initialize window";
        return LauncherResult::Cancelled;
    }

    if (!initVulkan()) {
        m_ErrorMessage = "Failed to initialize Vulkan";
        cleanup();
        return LauncherResult::Cancelled;
    }

    if (!initImGui()) {
        m_ErrorMessage = "Failed to initialize ImGui";
        cleanup();
        return LauncherResult::Cancelled;
    }

    m_Initialized = true;

    // Main loop
    while (!glfwWindowShouldClose(m_Window) && m_Result == LauncherResult::None) {
        glfwPollEvents();
        drawFrame();
    }

    // If window was closed without selection
    if (m_Result == LauncherResult::None) {
        m_Result = LauncherResult::Cancelled;
    }

    cleanup();
    return m_Result;
}

bool ProjectLauncher::initWindow() {
    if (!glfwInit()) {
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    m_Window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "MiEngine2 - Project Launcher", nullptr, nullptr);
    if (!m_Window) {
        glfwTerminate();
        return false;
    }

    return true;
}

bool ProjectLauncher::initVulkan() {
    // Create Vulkan instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MiEngine2 Launcher";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "MiEngine2";
    appInfo.engineVersion = VK_MAKE_VERSION(2, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    createInfo.enabledLayerCount = 0;

    if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS) {
        return false;
    }

    // Create surface
    if (glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface) != VK_SUCCESS) {
        return false;
    }

    // Pick physical device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());
    m_PhysicalDevice = devices[0]; // Just use first device for launcher

    // Find queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, m_Surface, &presentSupport);
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
            m_QueueFamily = i;
            break;
        }
    }

    // Create logical device
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_QueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = 1;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_Device) != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_GraphicsQueue);

    // Create swapchain
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities);

    m_SwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    m_SwapchainExtent = { WINDOW_WIDTH, WINDOW_HEIGHT };

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = m_Surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = m_SwapchainFormat;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = m_SwapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_Device, &swapchainInfo, nullptr, &m_Swapchain) != VK_SUCCESS) {
        return false;
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
    m_SwapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_SwapchainImages.data());

    // Create image views
    m_SwapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_SwapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_SwapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_SwapchainImageViews[i]) != VK_SUCCESS) {
            return false;
        }
    }

    // Create render pass
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_SwapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        return false;
    }

    // Create framebuffers
    m_Framebuffers.resize(m_SwapchainImageViews.size());
    for (size_t i = 0; i < m_SwapchainImageViews.size(); i++) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &m_SwapchainImageViews[i];
        framebufferInfo.width = m_SwapchainExtent.width;
        framebufferInfo.height = m_SwapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_Framebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_QueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        return false;
    }

    // Create command buffers
    m_CommandBuffers.resize(m_Framebuffers.size());
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

    if (vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS) {
        return false;
    }

    // Create sync objects
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFence) != VK_SUCCESS) {
        return false;
    }

    // Create descriptor pool for ImGui
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo descPoolInfo{};
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descPoolInfo.maxSets = 1000;
    descPoolInfo.poolSizeCount = 11;
    descPoolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(m_Device, &descPoolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool ProjectLauncher::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Customize style for launcher
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.ItemSpacing = ImVec2(12, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.WindowPadding = ImVec2(15, 15);

    ImGui_ImplGlfw_InitForVulkan(m_Window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = m_Instance;
    initInfo.PhysicalDevice = m_PhysicalDevice;
    initInfo.Device = m_Device;
    initInfo.QueueFamily = m_QueueFamily;
    initInfo.Queue = m_GraphicsQueue;
    initInfo.DescriptorPool = m_DescriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = static_cast<uint32_t>(m_SwapchainImages.size());
    initInfo.PipelineInfoMain.RenderPass = m_RenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        return false;
    }

    return true;
}

void ProjectLauncher::cleanup() {
    // Wait for device to be idle before cleanup
    if (m_Device != VK_NULL_HANDLE && m_Initialized) {
        VkResult result = vkDeviceWaitIdle(m_Device);
        if (result != VK_SUCCESS) {
            std::cerr << "Warning: vkDeviceWaitIdle failed during cleanup" << std::endl;
        }
    }

    // Shutdown ImGui first (before destroying Vulkan resources it depends on)
    if (m_Initialized) {
        // End any pending frame before shutdown
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        m_Initialized = false;
    }

    // Destroy Vulkan resources in reverse order of creation
    if (m_Device != VK_NULL_HANDLE) {
        if (m_InFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_Device, m_InFlightFence, nullptr);
            m_InFlightFence = VK_NULL_HANDLE;
        }
        if (m_RenderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_Device, m_RenderFinishedSemaphore, nullptr);
            m_RenderFinishedSemaphore = VK_NULL_HANDLE;
        }
        if (m_ImageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_Device, m_ImageAvailableSemaphore, nullptr);
            m_ImageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (m_CommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
            m_CommandPool = VK_NULL_HANDLE;
        }
        for (auto framebuffer : m_Framebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
            }
        }
        m_Framebuffers.clear();

        if (m_RenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }
        for (auto imageView : m_SwapchainImageViews) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(m_Device, imageView, nullptr);
            }
        }
        m_SwapchainImageViews.clear();

        if (m_Swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
        if (m_DescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
    }

    if (m_Instance != VK_NULL_HANDLE) {
        if (m_Surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
            m_Surface = VK_NULL_HANDLE;
        }
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
    }

    if (m_Window) {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    // Terminate GLFW - the Application will reinitialize it
    glfwTerminate();
}

void ProjectLauncher::drawFrame() {
    vkWaitForFences(m_Device, 1, &m_InFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_Device, 1, &m_InFlightFence);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_ImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    vkResetCommandBuffer(m_CommandBuffers[imageIndex], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(m_CommandBuffers[imageIndex], &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_Framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_SwapchainExtent;

    VkClearValue clearColor = { {{0.1f, 0.1f, 0.12f, 1.0f}} };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(m_CommandBuffers[imageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Draw ImGui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    drawLauncherUI();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_CommandBuffers[imageIndex]);

    vkCmdEndRenderPass(m_CommandBuffers[imageIndex]);
    vkEndCommandBuffer(m_CommandBuffers[imageIndex]);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffers[imageIndex];

    VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphore };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFence);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(m_GraphicsQueue, &presentInfo);
}

void ProjectLauncher::drawLauncherUI() {
    // Full window panel
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT));
    ImGui::Begin("Project Launcher", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Header
    ImGui::PushFont(nullptr);
    ImGui::SetCursorPosX((WINDOW_WIDTH - ImGui::CalcTextSize("MiEngine2").x * 2) / 2);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::Text("MiEngine2");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();

    ImGui::Separator();
    ImGui::Spacing();

    // Left panel - Recent Projects
    ImGui::BeginChild("LeftPanel", ImVec2(500, 450), true);
    ImGui::Text("Recent Projects");
    ImGui::Separator();
    drawProjectList();
    ImGui::EndChild();

    // Right panel - Actions
    ImGui::SameLine();
    ImGui::BeginChild("RightPanel", ImVec2(260, 450), true);
    ImGui::Text("Actions");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("New Project", ImVec2(-1, 40))) {
        m_ShowNewProjectDialog = true;
        // Set default path
        strcpy_s(m_NewProjectPath, "C:\\Projects");
    }

    ImGui::Spacing();

    if (ImGui::Button("Open Project", ImVec2(-1, 40))) {
        std::string path;
        if (showOpenFileDialog(path)) {
            if (ProjectManager::getInstance().openProject(path)) {
                m_Result = LauncherResult::ProjectOpened;
            } else {
                m_ErrorMessage = "Failed to open project";
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Game Mode Selection
    ImGui::Text("Game Mode:");
    const char* gameModes[] = {
        "1. Flappy Bird",
        "2. Editor Mode",
        "3. Minecraft Mode",
        "4. Shadow Test",
        "5. Point Light Test",
        "6. Water Test",
        "7. Draw Call Test",
        "8. Physics Test",
        "9. Skeletal Animation",
        "10. Ray Tracing Test",
        "11. Virtual Geo Test"
    };
    int currentMode = m_SelectedGameMode - 1; // Convert to 0-based index
    if (currentMode < 0) currentMode = 1; // Default to Editor
    if (currentMode > 10) currentMode = 10;

    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##gamemode", &currentMode, gameModes, IM_ARRAYSIZE(gameModes))) {
        m_SelectedGameMode = currentMode + 1; // Convert back to 1-based
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Quit", ImVec2(-1, 30))) {
        m_Result = LauncherResult::Cancelled;
    }

    ImGui::EndChild();

    // Dialogs
    if (m_ShowNewProjectDialog) {
        drawNewProjectDialog();
    }

    ImGui::End();
}

void ProjectLauncher::drawProjectList() {
    auto& pm = ProjectManager::getInstance();
    const auto& recentProjects = pm.getRecentProjects();

    if (recentProjects.empty()) {
        ImGui::TextDisabled("No recent projects");
        ImGui::TextDisabled("Create a new project to get started");
        return;
    }

    for (size_t i = 0; i < recentProjects.size(); ++i) {
        const auto& project = recentProjects[i];

        ImGui::PushID(static_cast<int>(i));

        // Check if project still exists
        bool exists = fs::exists(project.path);

        if (!exists) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }

        if (ImGui::Selectable(("##project" + std::to_string(i)).c_str(), false, 0, ImVec2(0, 50))) {
            if (exists) {
                if (pm.openProject(project.path)) {
                    m_Result = LauncherResult::ProjectOpened;
                } else {
                    m_ErrorMessage = "Failed to open project";
                }
            }
        }

        // Draw project info
        ImGui::SameLine(10);
        ImGui::BeginGroup();
        ImGui::Text("%s", project.name.c_str());
        ImGui::TextDisabled("%s", project.path.c_str());
        if (!exists) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "(Missing)");
        }
        ImGui::EndGroup();

        if (!exists) {
            ImGui::PopStyleColor();
        }

        // Context menu - use explicit ID to avoid assertion
        std::string contextMenuId = "context_menu_" + std::to_string(i);
        if (ImGui::BeginPopupContextItem(contextMenuId.c_str())) {
            if (ImGui::MenuItem("Remove from list")) {
                pm.removeRecentProject(project.path);
            }
            if (ImGui::MenuItem("Open in Explorer", nullptr, false, exists)) {
#ifdef _WIN32
                fs::path dir = fs::path(project.path).parent_path();
                ShellExecuteW(nullptr, L"explore", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        ImGui::Separator();
    }
}

void ProjectLauncher::drawNewProjectDialog() {
    ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH / 2 - 250, WINDOW_HEIGHT / 2 - 175), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_Always);

    if (ImGui::Begin("New Project", &m_ShowNewProjectDialog, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Create a new MiEngine2 project");
        ImGui::Separator();
        ImGui::Spacing();

        // Project name
        ImGui::Text("Project Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##name", m_NewProjectName, sizeof(m_NewProjectName));

        ImGui::Spacing();

        // Project location
        ImGui::Text("Location:");
        ImGui::SetNextItemWidth(-70);
        ImGui::InputText("##path", m_NewProjectPath, sizeof(m_NewProjectPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
            std::string path;
            if (showFolderBrowserDialog(path)) {
                strcpy_s(m_NewProjectPath, path.c_str());
            }
        }

        ImGui::Spacing();

        // Author (optional)
        ImGui::Text("Author (optional):");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##author", m_NewProjectAuthor, sizeof(m_NewProjectAuthor));

        ImGui::Spacing();

        // Description (optional)
        ImGui::Text("Description (optional):");
        ImGui::InputTextMultiline("##desc", m_NewProjectDescription, sizeof(m_NewProjectDescription),
            ImVec2(-1, 60));

        ImGui::Spacing();

        // Show resulting path
        if (strlen(m_NewProjectName) > 0 && strlen(m_NewProjectPath) > 0) {
            fs::path resultPath = fs::path(m_NewProjectPath) / m_NewProjectName;
            ImGui::TextDisabled("Project will be created at:");
            ImGui::TextDisabled("%s", resultPath.string().c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Buttons
        bool canCreate = strlen(m_NewProjectName) > 0 && strlen(m_NewProjectPath) > 0;

        if (!canCreate) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Create", ImVec2(120, 30))) {
            auto& pm = ProjectManager::getInstance();
            if (pm.createProject(m_NewProjectName, m_NewProjectPath)) {
                // Set additional info
                if (pm.getCurrentProject()) {
                    pm.getCurrentProject()->setAuthor(m_NewProjectAuthor);
                    pm.getCurrentProject()->setDescription(m_NewProjectDescription);
                    pm.saveProject();
                }
                m_ShowNewProjectDialog = false;
                m_Result = LauncherResult::ProjectOpened;
            } else {
                m_ErrorMessage = "Failed to create project. Check if directory already exists.";
            }
        }

        if (!canCreate) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 30))) {
            m_ShowNewProjectDialog = false;
        }

        // Show error if any
        if (!m_ErrorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", m_ErrorMessage.c_str());
        }
    }
    ImGui::End();
}

void ProjectLauncher::drawOpenProjectDialog() {
    // This is handled via native file dialog
}

bool ProjectLauncher::showFolderBrowserDialog(std::string& outPath) {
#ifdef _WIN32
    BROWSEINFOW bi = {};
    bi.lpszTitle = L"Select Project Location";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            // Convert to UTF-8
            int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
            std::string result(size - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, path, -1, &result[0], size, nullptr, nullptr);
            outPath = result;
            CoTaskMemFree(pidl);
            return true;
        }
        CoTaskMemFree(pidl);
    }
#endif
    return false;
}

bool ProjectLauncher::showOpenFileDialog(std::string& outPath) {
#ifdef _WIN32
    wchar_t filename[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"MiEngine2 Project (*.miproj)\0*.miproj\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Open Project";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"miproj";

    if (GetOpenFileNameW(&ofn)) {
        // Convert to UTF-8
        int size = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], size, nullptr, nullptr);
        outPath = result;
        return true;
    }
#endif
    return false;
}
