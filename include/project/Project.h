#pragma once

#include <string>
#include <filesystem>
#include <vector>
#include <chrono>

namespace fs = std::filesystem;

// Project metadata and paths
struct ProjectInfo {
    std::string name;
    std::string description;
    std::string version = "1.0.0";
    std::string engineVersion = "2.0.0";
    std::string author;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point modifiedAt;
};

class Project {
public:
    Project() = default;
    Project(const std::string& name, const fs::path& projectPath);

    // Project info
    const std::string& getName() const { return m_Info.name; }
    void setName(const std::string& name) { m_Info.name = name; }

    const std::string& getDescription() const { return m_Info.description; }
    void setDescription(const std::string& desc) { m_Info.description = desc; }

    const std::string& getAuthor() const { return m_Info.author; }
    void setAuthor(const std::string& author) { m_Info.author = author; }

    const ProjectInfo& getInfo() const { return m_Info; }
    ProjectInfo& getInfo() { return m_Info; }

    // Path accessors
    const fs::path& getProjectPath() const { return m_ProjectPath; }
    fs::path getProjectFilePath() const { return m_ProjectPath / (m_Info.name + ".miproj"); }

    // Standard project directories
    fs::path getAssetsPath() const { return m_ProjectPath / "Assets"; }
    fs::path getModelsPath() const { return m_ProjectPath / "Assets" / "Models"; }
    fs::path getTexturesPath() const { return m_ProjectPath / "Assets" / "Textures"; }
    fs::path getShadersPath() const { return m_ProjectPath / "Assets" / "Shaders"; }
    fs::path getHDRPath() const { return m_ProjectPath / "Assets" / "HDR"; }
    fs::path getScenesPath() const { return m_ProjectPath / "Scenes"; }
    fs::path getScriptsPath() const { return m_ProjectPath / "Scripts"; }
    fs::path getConfigPath() const { return m_ProjectPath / "Config"; }
    fs::path getCachePath() const { return m_ProjectPath / "Cache"; }

    // State
    bool isValid() const { return !m_Info.name.empty() && fs::exists(m_ProjectPath); }
    bool isDirty() const { return m_IsDirty; }
    void markDirty() { m_IsDirty = true; }
    void clearDirty() { m_IsDirty = false; }

    // Recent scenes
    void addRecentScene(const std::string& scenePath);
    const std::vector<std::string>& getRecentScenes() const { return m_RecentScenes; }

private:
    ProjectInfo m_Info;
    fs::path m_ProjectPath;
    bool m_IsDirty = false;
    std::vector<std::string> m_RecentScenes;
};
