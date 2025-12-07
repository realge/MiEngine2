#include "asset/AssetRegistry.h"
#include "asset/MeshCache.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <random>
#include <iomanip>

namespace MiEngine {

AssetRegistry& AssetRegistry::getInstance() {
    static AssetRegistry instance;
    return instance;
}

std::string AssetRegistry::generateUuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t a = dis(gen);
    uint64_t b = dis(gen);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << ((a >> 32) & 0xFFFFFFFF);
    oss << std::setw(4) << ((a >> 16) & 0xFFFF);
    oss << std::setw(4) << (a & 0xFFFF);
    oss << std::setw(4) << ((b >> 48) & 0xFFFF);
    oss << std::setw(12) << (b & 0xFFFFFFFFFFFF);
    return oss.str();
}

// Simple JSON helpers (no external library)
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
        } else {
            size_t end = json.find_first_of(",}\n]", start);
            std::string value = json.substr(start, end - start);
            // Trim whitespace
            size_t valueEnd = value.find_last_not_of(" \t\n\r");
            if (valueEnd != std::string::npos) {
                value = value.substr(0, valueEnd + 1);
            }
            return value;
        }
    }

    std::vector<std::string> extractJsonArray(const std::string& arrayStr) {
        std::vector<std::string> result;
        if (arrayStr.empty() || arrayStr[0] != '[') return result;

        int depth = 0;
        size_t objStart = 0;
        bool inObject = false;

        for (size_t i = 0; i < arrayStr.size(); ++i) {
            char c = arrayStr[i];
            if (c == '{') {
                if (depth == 0) {
                    objStart = i;
                    inObject = true;
                }
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 0 && inObject) {
                    result.push_back(arrayStr.substr(objStart, i - objStart + 1));
                    inObject = false;
                }
            }
        }
        return result;
    }
}

void AssetRegistry::loadFromProject(const fs::path& projectPath) {
    clear();
    m_projectPath = projectPath;

    fs::path registryFile = getRegistryFilePath();
    if (!fs::exists(registryFile)) {
        std::cout << "AssetRegistry: No registry file found, starting fresh" << std::endl;
        return;
    }

    std::ifstream file(registryFile);
    if (!file.is_open()) {
        std::cerr << "AssetRegistry: Failed to open registry file" << std::endl;
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    // Find assets array
    size_t assetsPos = json.find("\"assets\"");
    if (assetsPos == std::string::npos) {
        return;
    }

    size_t arrayStart = json.find('[', assetsPos);
    size_t arrayEnd = json.find_last_of(']');
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos) {
        return;
    }

    std::string assetsArray = json.substr(arrayStart, arrayEnd - arrayStart + 1);
    auto assetObjects = extractJsonArray(assetsArray);

    for (const auto& obj : assetObjects) {
        AssetEntry entry;
        entry.uuid = extractJsonValue(obj, "uuid");
        entry.name = extractJsonValue(obj, "name");
        entry.projectPath = extractJsonValue(obj, "projectPath");
        entry.cachePath = extractJsonValue(obj, "cachePath");
        entry.type = stringToAssetType(extractJsonValue(obj, "type"));

        std::string importTimeStr = extractJsonValue(obj, "importTime");
        entry.importTime = importTimeStr.empty() ? 0 : std::stoull(importTimeStr);

        std::string modTimeStr = extractJsonValue(obj, "sourceModTime");
        entry.sourceModTime = modTimeStr.empty() ? 0 : std::stoull(modTimeStr);

        std::string cacheValidStr = extractJsonValue(obj, "cacheValid");
        entry.cacheValid = (cacheValidStr == "true");

        if (!entry.uuid.empty()) {
            m_assets.push_back(entry);
        }
    }

    rebuildIndex();
    std::cout << "AssetRegistry: Loaded " << m_assets.size() << " assets" << std::endl;
}

void AssetRegistry::save() {
    if (m_projectPath.empty()) {
        std::cerr << "AssetRegistry: No project path set" << std::endl;
        return;
    }

    std::ofstream file(getRegistryFilePath());
    if (!file.is_open()) {
        std::cerr << "AssetRegistry: Failed to create registry file" << std::endl;
        return;
    }

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"assets\": [\n";

    for (size_t i = 0; i < m_assets.size(); ++i) {
        const auto& entry = m_assets[i];
        file << "    {\n";
        file << "      \"uuid\": \"" << escapeJson(entry.uuid) << "\",\n";
        file << "      \"name\": \"" << escapeJson(entry.name) << "\",\n";
        file << "      \"projectPath\": \"" << escapeJson(entry.projectPath) << "\",\n";
        file << "      \"cachePath\": \"" << escapeJson(entry.cachePath) << "\",\n";
        file << "      \"type\": \"" << assetTypeToString(entry.type) << "\",\n";
        file << "      \"importTime\": " << entry.importTime << ",\n";
        file << "      \"sourceModTime\": " << entry.sourceModTime << ",\n";
        file << "      \"cacheValid\": " << (entry.cacheValid ? "true" : "false") << "\n";
        file << "    }";
        if (i < m_assets.size() - 1) file << ",";
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    m_dirty = false;
    std::cout << "AssetRegistry: Saved " << m_assets.size() << " assets" << std::endl;
}

void AssetRegistry::clear() {
    m_assets.clear();
    m_uuidIndex.clear();
    m_pathIndex.clear();
    m_dirty = false;
}

void AssetRegistry::rebuildIndex() {
    m_uuidIndex.clear();
    m_pathIndex.clear();

    for (size_t i = 0; i < m_assets.size(); ++i) {
        m_uuidIndex[m_assets[i].uuid] = i;
        m_pathIndex[m_assets[i].projectPath] = i;
    }
}

const AssetEntry* AssetRegistry::findByUuid(const std::string& uuid) const {
    auto it = m_uuidIndex.find(uuid);
    if (it != m_uuidIndex.end()) {
        return &m_assets[it->second];
    }
    return nullptr;
}

const AssetEntry* AssetRegistry::findByPath(const std::string& projectPath) const {
    auto it = m_pathIndex.find(projectPath);
    if (it != m_pathIndex.end()) {
        return &m_assets[it->second];
    }
    return nullptr;
}

std::vector<AssetEntry> AssetRegistry::getAssetsByType(AssetType type) const {
    std::vector<AssetEntry> result;
    for (const auto& entry : m_assets) {
        if (entry.type == type) {
            result.push_back(entry);
        }
    }
    return result;
}

void AssetRegistry::addAsset(const AssetEntry& entry) {
    // Check for duplicate
    if (m_uuidIndex.count(entry.uuid) > 0) {
        std::cerr << "AssetRegistry: Asset with UUID already exists: " << entry.uuid << std::endl;
        return;
    }

    m_assets.push_back(entry);
    size_t idx = m_assets.size() - 1;
    m_uuidIndex[entry.uuid] = idx;
    m_pathIndex[entry.projectPath] = idx;
    m_dirty = true;
}

void AssetRegistry::updateAsset(const AssetEntry& entry) {
    auto it = m_uuidIndex.find(entry.uuid);
    if (it != m_uuidIndex.end()) {
        // Update path index if path changed
        const std::string& oldPath = m_assets[it->second].projectPath;
        if (oldPath != entry.projectPath) {
            m_pathIndex.erase(oldPath);
            m_pathIndex[entry.projectPath] = it->second;
        }

        m_assets[it->second] = entry;
        m_dirty = true;
    }
}

void AssetRegistry::removeAsset(const std::string& uuid) {
    auto it = m_uuidIndex.find(uuid);
    if (it == m_uuidIndex.end()) {
        return;
    }

    size_t idx = it->second;
    const std::string& path = m_assets[idx].projectPath;

    m_pathIndex.erase(path);
    m_uuidIndex.erase(uuid);

    // Remove from vector (swap with last and pop)
    if (idx < m_assets.size() - 1) {
        m_assets[idx] = std::move(m_assets.back());
        // Update indices for moved element
        m_uuidIndex[m_assets[idx].uuid] = idx;
        m_pathIndex[m_assets[idx].projectPath] = idx;
    }
    m_assets.pop_back();
    m_dirty = true;
}

void AssetRegistry::invalidateCache(const std::string& uuid) {
    auto it = m_uuidIndex.find(uuid);
    if (it != m_uuidIndex.end()) {
        m_assets[it->second].cacheValid = false;
        m_dirty = true;
    }
}

void AssetRegistry::validateCache(const std::string& uuid) {
    auto it = m_uuidIndex.find(uuid);
    if (it != m_uuidIndex.end()) {
        m_assets[it->second].cacheValid = true;
        m_dirty = true;
    }
}

void AssetRegistry::refreshAll() {
    for (auto& entry : m_assets) {
        fs::path sourcePath = resolveAssetPath(entry.projectPath);
        fs::path cachePath = resolveCachePath(entry.cachePath);

        entry.cacheValid = MeshCache::isValid(cachePath, sourcePath);
    }
    m_dirty = true;
}

fs::path AssetRegistry::resolveAssetPath(const std::string& projectPath) const {
    return getAssetsPath() / projectPath;
}

fs::path AssetRegistry::resolveCachePath(const std::string& cachePath) const {
    return getCachePath() / cachePath;
}

} // namespace MiEngine
