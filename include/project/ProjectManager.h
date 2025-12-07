#pragma once

#include "Project.h"
#include <memory>
#include <functional>
#include <optional>

// Recent project entry for the launcher
struct RecentProjectEntry {
    std::string name;
    std::string path;
    std::chrono::system_clock::time_point lastOpened;
};

class ProjectManager {
public:
    static ProjectManager& getInstance();

    // Project operations
    bool createProject(const std::string& name, const fs::path& directory);
    bool openProject(const fs::path& projectFilePath);
    bool saveProject();
    void closeProject();

    // Current project
    Project* getCurrentProject() { return m_CurrentProject.get(); }
    const Project* getCurrentProject() const { return m_CurrentProject.get(); }
    bool hasProject() const { return m_CurrentProject != nullptr; }

    // Recent projects
    const std::vector<RecentProjectEntry>& getRecentProjects() const { return m_RecentProjects; }
    void clearRecentProjects();
    void removeRecentProject(const std::string& path);

    // Engine paths (where engine assets are located)
    void setEnginePath(const fs::path& path) { m_EnginePath = path; }
    const fs::path& getEnginePath() const { return m_EnginePath; }
    fs::path getEngineAssetsPath() const { return m_EnginePath; }
    fs::path getEngineModelsPath() const { return m_EnginePath / "models"; }
    fs::path getEngineTexturesPath() const { return m_EnginePath / "texture"; }
    fs::path getEngineShadersPath() const { return m_EnginePath / "shaders"; }
    fs::path getEngineHDRPath() const { return m_EnginePath / "hdr"; }

    // User data path (for storing recent projects list, etc.)
    fs::path getUserDataPath() const;

    // Asset resolution - checks project first, then engine
    std::optional<fs::path> resolveAssetPath(const std::string& relativePath) const;
    std::optional<fs::path> resolveModelPath(const std::string& filename) const;
    std::optional<fs::path> resolveTexturePath(const std::string& filename) const;
    std::optional<fs::path> resolveShaderPath(const std::string& filename) const;
    std::optional<fs::path> resolveHDRPath(const std::string& filename) const;

private:
    ProjectManager();
    ~ProjectManager() = default;
    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    bool createProjectDirectories(const fs::path& projectPath);
    void loadRecentProjects();
    void saveRecentProjects();
    void addToRecentProjects(const Project& project);

    std::unique_ptr<Project> m_CurrentProject;
    std::vector<RecentProjectEntry> m_RecentProjects;
    fs::path m_EnginePath;
};
