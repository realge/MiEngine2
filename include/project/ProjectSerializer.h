#pragma once

#include "Project.h"
#include "ProjectManager.h"
#include <string>

class ProjectSerializer {
public:
    // Project file (.miproj)
    static bool saveProject(const Project& project, const fs::path& filePath);
    static bool loadProject(Project& project, const fs::path& filePath);

    // Recent projects list
    static bool saveRecentProjects(const std::vector<RecentProjectEntry>& entries, const fs::path& filePath);
    static bool loadRecentProjects(std::vector<RecentProjectEntry>& entries, const fs::path& filePath);

private:
    // JSON helpers
    static std::string timePointToString(const std::chrono::system_clock::time_point& tp);
    static std::chrono::system_clock::time_point stringToTimePoint(const std::string& str);
};
