#include "project/ProjectSerializer.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <regex>

// Simple JSON helpers (no external library needed)
namespace {
    std::string escapeJson(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }

    std::string unescapeJson(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '\\' && i + 1 < str.size()) {
                switch (str[i + 1]) {
                    case '"': result += '"'; ++i; break;
                    case '\\': result += '\\'; ++i; break;
                    case 'n': result += '\n'; ++i; break;
                    case 'r': result += '\r'; ++i; break;
                    case 't': result += '\t'; ++i; break;
                    default: result += str[i]; break;
                }
            } else {
                result += str[i];
            }
        }
        return result;
    }

    std::string extractJsonValue(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";

        size_t colonPos = json.find(':', keyPos);
        if (colonPos == std::string::npos) return "";

        size_t start = json.find_first_not_of(" \t\n\r", colonPos + 1);
        if (start == std::string::npos) return "";

        if (json[start] == '"') {
            size_t end = start + 1;
            while (end < json.size() && (json[end] != '"' || json[end - 1] == '\\')) {
                ++end;
            }
            return unescapeJson(json.substr(start + 1, end - start - 1));
        } else if (json[start] == '[') {
            // Array - find matching bracket
            int depth = 1;
            size_t end = start + 1;
            while (end < json.size() && depth > 0) {
                if (json[end] == '[') ++depth;
                else if (json[end] == ']') --depth;
                ++end;
            }
            return json.substr(start, end - start);
        } else {
            size_t end = json.find_first_of(",}\n", start);
            return json.substr(start, end - start);
        }
    }

    std::vector<std::string> extractJsonArray(const std::string& arrayStr) {
        std::vector<std::string> result;
        if (arrayStr.empty() || arrayStr[0] != '[') return result;

        size_t i = 1;
        while (i < arrayStr.size()) {
            // Skip whitespace
            while (i < arrayStr.size() && std::isspace(arrayStr[i])) ++i;
            if (i >= arrayStr.size() || arrayStr[i] == ']') break;

            if (arrayStr[i] == '"') {
                size_t start = i + 1;
                size_t end = start;
                while (end < arrayStr.size() && (arrayStr[end] != '"' || arrayStr[end - 1] == '\\')) {
                    ++end;
                }
                result.push_back(unescapeJson(arrayStr.substr(start, end - start)));
                i = end + 1;
            } else if (arrayStr[i] == '{') {
                // Object in array
                int depth = 1;
                size_t start = i;
                ++i;
                while (i < arrayStr.size() && depth > 0) {
                    if (arrayStr[i] == '{') ++depth;
                    else if (arrayStr[i] == '}') --depth;
                    ++i;
                }
                result.push_back(arrayStr.substr(start, i - start));
            }

            // Skip to next element
            while (i < arrayStr.size() && arrayStr[i] != ',' && arrayStr[i] != ']') ++i;
            if (i < arrayStr.size() && arrayStr[i] == ',') ++i;
        }
        return result;
    }
}

std::string ProjectSerializer::timePointToString(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::chrono::system_clock::time_point ProjectSerializer::stringToTimePoint(const std::string& str) {
    std::tm tm = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) {
        return std::chrono::system_clock::now();
    }
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

bool ProjectSerializer::saveProject(const Project& project, const fs::path& filePath) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    const auto& info = project.getInfo();

    file << "{\n";
    file << "  \"name\": \"" << escapeJson(info.name) << "\",\n";
    file << "  \"description\": \"" << escapeJson(info.description) << "\",\n";
    file << "  \"version\": \"" << escapeJson(info.version) << "\",\n";
    file << "  \"engineVersion\": \"" << escapeJson(info.engineVersion) << "\",\n";
    file << "  \"author\": \"" << escapeJson(info.author) << "\",\n";
    file << "  \"createdAt\": \"" << timePointToString(info.createdAt) << "\",\n";
    file << "  \"modifiedAt\": \"" << timePointToString(std::chrono::system_clock::now()) << "\",\n";

    // Recent scenes
    file << "  \"recentScenes\": [";
    const auto& scenes = project.getRecentScenes();
    for (size_t i = 0; i < scenes.size(); ++i) {
        file << "\"" << escapeJson(scenes[i]) << "\"";
        if (i < scenes.size() - 1) file << ", ";
    }
    file << "]\n";

    file << "}\n";

    return file.good();
}

bool ProjectSerializer::loadProject(Project& project, const fs::path& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    auto& info = project.getInfo();
    info.name = extractJsonValue(json, "name");
    info.description = extractJsonValue(json, "description");
    info.version = extractJsonValue(json, "version");
    info.engineVersion = extractJsonValue(json, "engineVersion");
    info.author = extractJsonValue(json, "author");
    info.createdAt = stringToTimePoint(extractJsonValue(json, "createdAt"));
    info.modifiedAt = stringToTimePoint(extractJsonValue(json, "modifiedAt"));

    // Recent scenes
    std::string scenesArray = extractJsonValue(json, "recentScenes");
    auto scenes = extractJsonArray(scenesArray);
    for (const auto& scene : scenes) {
        project.addRecentScene(scene);
    }

    return true;
}

bool ProjectSerializer::saveRecentProjects(const std::vector<RecentProjectEntry>& entries, const fs::path& filePath) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"recentProjects\": [\n";

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        file << "    {\n";
        file << "      \"name\": \"" << escapeJson(entry.name) << "\",\n";
        file << "      \"path\": \"" << escapeJson(entry.path) << "\",\n";
        file << "      \"lastOpened\": \"" << timePointToString(entry.lastOpened) << "\"\n";
        file << "    }";
        if (i < entries.size() - 1) file << ",";
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    return file.good();
}

bool ProjectSerializer::loadRecentProjects(std::vector<RecentProjectEntry>& entries, const fs::path& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    entries.clear();

    std::string projectsArray = extractJsonValue(json, "recentProjects");
    auto projectObjects = extractJsonArray(projectsArray);

    for (const auto& obj : projectObjects) {
        RecentProjectEntry entry;
        entry.name = extractJsonValue(obj, "name");
        entry.path = extractJsonValue(obj, "path");
        entry.lastOpened = stringToTimePoint(extractJsonValue(obj, "lastOpened"));
        entries.push_back(entry);
    }

    return true;
}
