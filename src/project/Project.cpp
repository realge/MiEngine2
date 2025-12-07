#include "project/Project.h"
#include <algorithm>

Project::Project(const std::string& name, const fs::path& projectPath)
    : m_ProjectPath(projectPath)
{
    m_Info.name = name;
    m_Info.createdAt = std::chrono::system_clock::now();
    m_Info.modifiedAt = m_Info.createdAt;
}

void Project::addRecentScene(const std::string& scenePath) {
    // Remove if already exists
    auto it = std::find(m_RecentScenes.begin(), m_RecentScenes.end(), scenePath);
    if (it != m_RecentScenes.end()) {
        m_RecentScenes.erase(it);
    }

    // Add to front
    m_RecentScenes.insert(m_RecentScenes.begin(), scenePath);

    // Keep only last 10
    if (m_RecentScenes.size() > 10) {
        m_RecentScenes.resize(10);
    }

    markDirty();
}
