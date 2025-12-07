#include "project/ProjectManager.h"
#include "project/ProjectSerializer.h"
#include "asset/AssetRegistry.h"
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>
#endif

ProjectManager& ProjectManager::getInstance() {
    static ProjectManager instance;
    return instance;
}

ProjectManager::ProjectManager() {
    // Set engine path to current working directory by default
    m_EnginePath = fs::current_path();
    loadRecentProjects();
}

fs::path ProjectManager::getUserDataPath() const {
#ifdef _WIN32
    wchar_t* path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        fs::path result = fs::path(path) / "MiEngine2";
        CoTaskMemFree(path);

        // Create directory if it doesn't exist
        if (!fs::exists(result)) {
            fs::create_directories(result);
        }
        return result;
    }
#endif
    // Fallback to engine directory
    return m_EnginePath / "UserData";
}

bool ProjectManager::createProject(const std::string& name, const fs::path& directory) {
    // Create project path
    fs::path projectPath = directory / name;

    // Check if directory already exists
    if (fs::exists(projectPath)) {
        std::cerr << "Project directory already exists: " << projectPath << std::endl;
        return false;
    }

    // Create project directories
    if (!createProjectDirectories(projectPath)) {
        return false;
    }

    // Create project object
    m_CurrentProject = std::make_unique<Project>(name, projectPath);

    // Save project file
    if (!ProjectSerializer::saveProject(*m_CurrentProject, m_CurrentProject->getProjectFilePath())) {
        std::cerr << "Failed to save project file" << std::endl;
        return false;
    }

    // Add to recent projects
    addToRecentProjects(*m_CurrentProject);

    // Initialize asset registry for this project
    MiEngine::AssetRegistry::getInstance().loadFromProject(projectPath);

    std::cout << "Created project: " << name << " at " << projectPath << std::endl;
    return true;
}

bool ProjectManager::createProjectDirectories(const fs::path& projectPath) {
    try {
        // Create main project directory
        fs::create_directories(projectPath);

        // Create subdirectories
        fs::create_directories(projectPath / "Assets");
        fs::create_directories(projectPath / "Assets" / "Models");
        fs::create_directories(projectPath / "Assets" / "Textures");
        fs::create_directories(projectPath / "Assets" / "Shaders");
        fs::create_directories(projectPath / "Assets" / "HDR");
        fs::create_directories(projectPath / "Assets" / "Audio");
        fs::create_directories(projectPath / "Scenes");
        fs::create_directories(projectPath / "Scripts");
        fs::create_directories(projectPath / "Config");
        fs::create_directories(projectPath / "Cache");

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to create project directories: " << e.what() << std::endl;
        return false;
    }
}

bool ProjectManager::openProject(const fs::path& projectFilePath) {
    if (!fs::exists(projectFilePath)) {
        std::cerr << "Project file not found: " << projectFilePath << std::endl;
        return false;
    }

    // Close current project
    closeProject();

    // Get project directory from file path
    fs::path projectDir = projectFilePath.parent_path();
    std::string projectName = projectFilePath.stem().string();

    // Create project object
    m_CurrentProject = std::make_unique<Project>(projectName, projectDir);

    // Load project data
    if (!ProjectSerializer::loadProject(*m_CurrentProject, projectFilePath)) {
        std::cerr << "Failed to load project file" << std::endl;
        m_CurrentProject.reset();
        return false;
    }

    // Add to recent projects
    addToRecentProjects(*m_CurrentProject);

    // Initialize asset registry for this project
    MiEngine::AssetRegistry::getInstance().loadFromProject(projectDir);

    std::cout << "Opened project: " << m_CurrentProject->getName() << std::endl;
    return true;
}

bool ProjectManager::saveProject() {
    if (!m_CurrentProject) {
        return false;
    }

    if (!ProjectSerializer::saveProject(*m_CurrentProject, m_CurrentProject->getProjectFilePath())) {
        std::cerr << "Failed to save project" << std::endl;
        return false;
    }

    m_CurrentProject->clearDirty();
    return true;
}

void ProjectManager::closeProject() {
    if (m_CurrentProject) {
        if (m_CurrentProject->isDirty()) {
            saveProject();
        }
        m_CurrentProject.reset();
    }
}

void ProjectManager::loadRecentProjects() {
    fs::path recentFile = getUserDataPath() / "recent_projects.json";
    ProjectSerializer::loadRecentProjects(m_RecentProjects, recentFile);
}

void ProjectManager::saveRecentProjects() {
    fs::path recentFile = getUserDataPath() / "recent_projects.json";
    ProjectSerializer::saveRecentProjects(m_RecentProjects, recentFile);
}

void ProjectManager::addToRecentProjects(const Project& project) {
    // Remove if already exists
    removeRecentProject(project.getProjectFilePath().string());

    // Add to front
    RecentProjectEntry entry;
    entry.name = project.getName();
    entry.path = project.getProjectFilePath().string();
    entry.lastOpened = std::chrono::system_clock::now();

    m_RecentProjects.insert(m_RecentProjects.begin(), entry);

    // Keep only last 10
    if (m_RecentProjects.size() > 10) {
        m_RecentProjects.resize(10);
    }

    saveRecentProjects();
}

void ProjectManager::clearRecentProjects() {
    m_RecentProjects.clear();
    saveRecentProjects();
}

void ProjectManager::removeRecentProject(const std::string& path) {
    m_RecentProjects.erase(
        std::remove_if(m_RecentProjects.begin(), m_RecentProjects.end(),
            [&path](const RecentProjectEntry& e) { return e.path == path; }),
        m_RecentProjects.end()
    );
}

std::optional<fs::path> ProjectManager::resolveAssetPath(const std::string& relativePath) const {
    // Check project assets first
    if (m_CurrentProject) {
        fs::path projectAsset = m_CurrentProject->getAssetsPath() / relativePath;
        if (fs::exists(projectAsset)) {
            return projectAsset;
        }
    }

    // Fall back to engine assets
    fs::path engineAsset = m_EnginePath / relativePath;
    if (fs::exists(engineAsset)) {
        return engineAsset;
    }

    return std::nullopt;
}

std::optional<fs::path> ProjectManager::resolveModelPath(const std::string& filename) const {
    if (m_CurrentProject) {
        fs::path projectPath = m_CurrentProject->getModelsPath() / filename;
        if (fs::exists(projectPath)) {
            return projectPath;
        }
    }

    fs::path enginePath = getEngineModelsPath() / filename;
    if (fs::exists(enginePath)) {
        return enginePath;
    }

    return std::nullopt;
}

std::optional<fs::path> ProjectManager::resolveTexturePath(const std::string& filename) const {
    if (m_CurrentProject) {
        fs::path projectPath = m_CurrentProject->getTexturesPath() / filename;
        if (fs::exists(projectPath)) {
            return projectPath;
        }
    }

    fs::path enginePath = getEngineTexturesPath() / filename;
    if (fs::exists(enginePath)) {
        return enginePath;
    }

    return std::nullopt;
}

std::optional<fs::path> ProjectManager::resolveShaderPath(const std::string& filename) const {
    if (m_CurrentProject) {
        fs::path projectPath = m_CurrentProject->getShadersPath() / filename;
        if (fs::exists(projectPath)) {
            return projectPath;
        }
    }

    fs::path enginePath = getEngineShadersPath() / filename;
    if (fs::exists(enginePath)) {
        return enginePath;
    }

    return std::nullopt;
}

std::optional<fs::path> ProjectManager::resolveHDRPath(const std::string& filename) const {
    if (m_CurrentProject) {
        fs::path projectPath = m_CurrentProject->getHDRPath() / filename;
        if (fs::exists(projectPath)) {
            return projectPath;
        }
    }

    fs::path enginePath = getEngineHDRPath() / filename;
    if (fs::exists(enginePath)) {
        return enginePath;
    }

    return std::nullopt;
}
