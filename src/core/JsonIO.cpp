#include "core/JsonIO.h"
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cctype>

namespace MiEngine {

// ============================================================================
// JsonWriter Implementation
// ============================================================================

JsonWriter::JsonWriter() {
    m_InArrayStack.push_back(false);
}

void JsonWriter::writeIndent() {
    for (int i = 0; i < m_Indent; ++i) {
        m_Stream << "  ";
    }
}

void JsonWriter::writeCommaIfNeeded() {
    if (m_NeedComma) {
        m_Stream << ",";
    }
    m_Stream << "\n";
    m_NeedComma = true;
}

void JsonWriter::writeKey(const std::string& key) {
    writeCommaIfNeeded();
    writeIndent();
    m_Stream << "\"" << escapeString(key) << "\": ";
}

std::string JsonWriter::escapeString(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 32) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

void JsonWriter::beginObject() {
    if (!m_InArrayStack.empty() && m_InArrayStack.back()) {
        writeCommaIfNeeded();
        writeIndent();
    }
    m_Stream << "{";
    ++m_Indent;
    m_NeedComma = false;
    m_InArrayStack.push_back(false);
}

void JsonWriter::beginObject(const std::string& key) {
    writeKey(key);
    m_Stream << "{";
    ++m_Indent;
    m_NeedComma = false;
    m_InArrayStack.push_back(false);
}

void JsonWriter::endObject() {
    --m_Indent;
    m_Stream << "\n";
    writeIndent();
    m_Stream << "}";
    m_NeedComma = true;
    if (!m_InArrayStack.empty()) {
        m_InArrayStack.pop_back();
    }
}

void JsonWriter::beginArray(const std::string& key) {
    writeKey(key);
    m_Stream << "[";
    ++m_Indent;
    m_NeedComma = false;
    m_InArrayStack.push_back(true);
}

void JsonWriter::endArray() {
    --m_Indent;
    m_Stream << "\n";
    writeIndent();
    m_Stream << "]";
    m_NeedComma = true;
    if (!m_InArrayStack.empty()) {
        m_InArrayStack.pop_back();
    }
}

void JsonWriter::writeString(const std::string& key, const std::string& value) {
    writeKey(key);
    m_Stream << "\"" << escapeString(value) << "\"";
}

void JsonWriter::writeInt(const std::string& key, int value) {
    writeKey(key);
    m_Stream << value;
}

void JsonWriter::writeUInt(const std::string& key, uint32_t value) {
    writeKey(key);
    m_Stream << value;
}

void JsonWriter::writeUInt64(const std::string& key, uint64_t value) {
    writeKey(key);
    m_Stream << value;
}

void JsonWriter::writeFloat(const std::string& key, float value) {
    writeKey(key);
    if (std::isnan(value) || std::isinf(value)) {
        m_Stream << "null";
    } else {
        m_Stream << std::setprecision(7) << value;
    }
}

void JsonWriter::writeDouble(const std::string& key, double value) {
    writeKey(key);
    if (std::isnan(value) || std::isinf(value)) {
        m_Stream << "null";
    } else {
        m_Stream << std::setprecision(15) << value;
    }
}

void JsonWriter::writeBool(const std::string& key, bool value) {
    writeKey(key);
    m_Stream << (value ? "true" : "false");
}

void JsonWriter::writeNull(const std::string& key) {
    writeKey(key);
    m_Stream << "null";
}

void JsonWriter::writeVec2(const std::string& key, const glm::vec2& value) {
    writeKey(key);
    m_Stream << "[" << value.x << ", " << value.y << "]";
}

void JsonWriter::writeVec3(const std::string& key, const glm::vec3& value) {
    writeKey(key);
    m_Stream << "[" << value.x << ", " << value.y << ", " << value.z << "]";
}

void JsonWriter::writeVec4(const std::string& key, const glm::vec4& value) {
    writeKey(key);
    m_Stream << "[" << value.x << ", " << value.y << ", " << value.z << ", " << value.w << "]";
}

void JsonWriter::writeQuat(const std::string& key, const glm::quat& value) {
    writeKey(key);
    // Store as [w, x, y, z] for clarity
    m_Stream << "[" << value.w << ", " << value.x << ", " << value.y << ", " << value.z << "]";
}

void JsonWriter::writeMat4(const std::string& key, const glm::mat4& value) {
    writeKey(key);
    m_Stream << "[";
    for (int i = 0; i < 16; ++i) {
        if (i > 0) m_Stream << ", ";
        m_Stream << value[i / 4][i % 4];
    }
    m_Stream << "]";
}

void JsonWriter::writeArrayString(const std::string& value) {
    writeCommaIfNeeded();
    writeIndent();
    m_Stream << "\"" << escapeString(value) << "\"";
}

void JsonWriter::writeArrayInt(int value) {
    writeCommaIfNeeded();
    writeIndent();
    m_Stream << value;
}

void JsonWriter::writeArrayFloat(float value) {
    writeCommaIfNeeded();
    writeIndent();
    m_Stream << value;
}

void JsonWriter::beginArrayObject() {
    writeCommaIfNeeded();
    writeIndent();
    m_Stream << "{";
    ++m_Indent;
    m_NeedComma = false;
    m_InArrayStack.push_back(false);
}

std::string JsonWriter::toString() const {
    return m_Stream.str();
}

bool JsonWriter::saveToFile(const std::string& filePath) const {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }
    file << m_Stream.str();
    return file.good();
}

// ============================================================================
// JsonReader Implementation
// ============================================================================

JsonReader::JsonReader() = default;

JsonReader::JsonReader(const std::string& json) : m_Json(json) {}

bool JsonReader::loadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    m_Json = buffer.str();
    return true;
}

bool JsonReader::loadFromString(const std::string& json) {
    m_Json = json;
    return !m_Json.empty();
}

std::string JsonReader::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::string JsonReader::unescapeString(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            switch (str[i + 1]) {
                case '"':  result += '"'; ++i; break;
                case '\\': result += '\\'; ++i; break;
                case 'n':  result += '\n'; ++i; break;
                case 'r':  result += '\r'; ++i; break;
                case 't':  result += '\t'; ++i; break;
                case 'b':  result += '\b'; ++i; break;
                case 'f':  result += '\f'; ++i; break;
                case 'u':
                    if (i + 5 < str.size()) {
                        // Parse unicode escape (simplified - ASCII only)
                        std::string hex = str.substr(i + 2, 4);
                        int codepoint = std::stoi(hex, nullptr, 16);
                        if (codepoint < 128) {
                            result += static_cast<char>(codepoint);
                        }
                        i += 5;
                    }
                    break;
                default:
                    result += str[i];
                    break;
            }
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string JsonReader::extractValue(const std::string& key) const {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = m_Json.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = m_Json.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) return "";

    size_t start = m_Json.find_first_not_of(" \t\n\r", colonPos + 1);
    if (start == std::string::npos) return "";

    char firstChar = m_Json[start];

    if (firstChar == '"') {
        // String value
        size_t end = start + 1;
        while (end < m_Json.size()) {
            if (m_Json[end] == '"' && m_Json[end - 1] != '\\') {
                break;
            }
            ++end;
        }
        return m_Json.substr(start + 1, end - start - 1);
    } else if (firstChar == '[') {
        // Array
        int depth = 1;
        size_t end = start + 1;
        while (end < m_Json.size() && depth > 0) {
            if (m_Json[end] == '[') ++depth;
            else if (m_Json[end] == ']') --depth;
            else if (m_Json[end] == '"') {
                // Skip string content
                ++end;
                while (end < m_Json.size() && (m_Json[end] != '"' || m_Json[end - 1] == '\\')) {
                    ++end;
                }
            }
            ++end;
        }
        return m_Json.substr(start, end - start);
    } else if (firstChar == '{') {
        // Object
        int depth = 1;
        size_t end = start + 1;
        while (end < m_Json.size() && depth > 0) {
            if (m_Json[end] == '{') ++depth;
            else if (m_Json[end] == '}') --depth;
            else if (m_Json[end] == '"') {
                // Skip string content
                ++end;
                while (end < m_Json.size() && (m_Json[end] != '"' || m_Json[end - 1] == '\\')) {
                    ++end;
                }
            }
            ++end;
        }
        return m_Json.substr(start, end - start);
    } else {
        // Primitive (number, bool, null)
        size_t end = m_Json.find_first_of(",}\n\r]", start);
        if (end == std::string::npos) end = m_Json.size();
        return trim(m_Json.substr(start, end - start));
    }
}

bool JsonReader::hasKey(const std::string& key) const {
    std::string searchKey = "\"" + key + "\"";
    return m_Json.find(searchKey) != std::string::npos;
}

std::string JsonReader::getString(const std::string& key, const std::string& defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty()) return defaultValue;
    return unescapeString(value);
}

int JsonReader::getInt(const std::string& key, int defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty() || value == "null") return defaultValue;
    try {
        return std::stoi(value);
    } catch (...) {
        return defaultValue;
    }
}

uint32_t JsonReader::getUInt(const std::string& key, uint32_t defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty() || value == "null") return defaultValue;
    try {
        return static_cast<uint32_t>(std::stoul(value));
    } catch (...) {
        return defaultValue;
    }
}

uint64_t JsonReader::getUInt64(const std::string& key, uint64_t defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty() || value == "null") return defaultValue;
    try {
        return std::stoull(value);
    } catch (...) {
        return defaultValue;
    }
}

float JsonReader::getFloat(const std::string& key, float defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty() || value == "null") return defaultValue;
    try {
        return std::stof(value);
    } catch (...) {
        return defaultValue;
    }
}

double JsonReader::getDouble(const std::string& key, double defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty() || value == "null") return defaultValue;
    try {
        return std::stod(value);
    } catch (...) {
        return defaultValue;
    }
}

bool JsonReader::getBool(const std::string& key, bool defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty()) return defaultValue;
    return value == "true";
}

std::vector<float> JsonReader::parseFloatArray(const std::string& arrayStr) const {
    std::vector<float> result;
    if (arrayStr.empty() || arrayStr[0] != '[') return result;

    std::string content = arrayStr.substr(1, arrayStr.size() - 2);
    std::stringstream ss(content);
    std::string token;

    while (std::getline(ss, token, ',')) {
        token = trim(token);
        if (!token.empty()) {
            try {
                result.push_back(std::stof(token));
            } catch (...) {
                result.push_back(0.0f);
            }
        }
    }
    return result;
}

glm::vec2 JsonReader::getVec2(const std::string& key, const glm::vec2& defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty()) return defaultValue;

    auto floats = parseFloatArray(value);
    if (floats.size() >= 2) {
        return glm::vec2(floats[0], floats[1]);
    }
    return defaultValue;
}

glm::vec3 JsonReader::getVec3(const std::string& key, const glm::vec3& defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty()) return defaultValue;

    auto floats = parseFloatArray(value);
    if (floats.size() >= 3) {
        return glm::vec3(floats[0], floats[1], floats[2]);
    }
    return defaultValue;
}

glm::vec4 JsonReader::getVec4(const std::string& key, const glm::vec4& defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty()) return defaultValue;

    auto floats = parseFloatArray(value);
    if (floats.size() >= 4) {
        return glm::vec4(floats[0], floats[1], floats[2], floats[3]);
    }
    return defaultValue;
}

glm::quat JsonReader::getQuat(const std::string& key, const glm::quat& defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty()) return defaultValue;

    auto floats = parseFloatArray(value);
    if (floats.size() >= 4) {
        // Stored as [w, x, y, z]
        return glm::quat(floats[0], floats[1], floats[2], floats[3]);
    }
    return defaultValue;
}

glm::mat4 JsonReader::getMat4(const std::string& key, const glm::mat4& defaultValue) const {
    std::string value = extractValue(key);
    if (value.empty()) return defaultValue;

    auto floats = parseFloatArray(value);
    if (floats.size() >= 16) {
        glm::mat4 result;
        for (int i = 0; i < 16; ++i) {
            result[i / 4][i % 4] = floats[i];
        }
        return result;
    }
    return defaultValue;
}

std::vector<std::string> JsonReader::extractArrayElements(const std::string& arrayStr) const {
    std::vector<std::string> result;
    if (arrayStr.empty() || arrayStr[0] != '[') return result;

    size_t i = 1;
    while (i < arrayStr.size()) {
        // Skip whitespace
        while (i < arrayStr.size() && std::isspace(arrayStr[i])) ++i;
        if (i >= arrayStr.size() || arrayStr[i] == ']') break;

        size_t start = i;

        if (arrayStr[i] == '"') {
            // String element
            ++i;
            while (i < arrayStr.size() && (arrayStr[i] != '"' || arrayStr[i - 1] == '\\')) {
                ++i;
            }
            ++i; // Past closing quote
            result.push_back(arrayStr.substr(start, i - start));
        } else if (arrayStr[i] == '{') {
            // Object element
            int depth = 1;
            ++i;
            while (i < arrayStr.size() && depth > 0) {
                if (arrayStr[i] == '{') ++depth;
                else if (arrayStr[i] == '}') --depth;
                else if (arrayStr[i] == '"') {
                    ++i;
                    while (i < arrayStr.size() && (arrayStr[i] != '"' || arrayStr[i - 1] == '\\')) {
                        ++i;
                    }
                }
                ++i;
            }
            result.push_back(arrayStr.substr(start, i - start));
        } else if (arrayStr[i] == '[') {
            // Nested array
            int depth = 1;
            ++i;
            while (i < arrayStr.size() && depth > 0) {
                if (arrayStr[i] == '[') ++depth;
                else if (arrayStr[i] == ']') --depth;
                ++i;
            }
            result.push_back(arrayStr.substr(start, i - start));
        } else {
            // Primitive
            while (i < arrayStr.size() && arrayStr[i] != ',' && arrayStr[i] != ']') {
                ++i;
            }
            result.push_back(trim(arrayStr.substr(start, i - start)));
        }

        // Skip to next element
        while (i < arrayStr.size() && arrayStr[i] != ',' && arrayStr[i] != ']') ++i;
        if (i < arrayStr.size() && arrayStr[i] == ',') ++i;
    }

    return result;
}

std::vector<std::string> JsonReader::getStringArray(const std::string& key) const {
    std::vector<std::string> result;
    std::string arrayStr = extractValue(key);
    auto elements = extractArrayElements(arrayStr);

    for (const auto& elem : elements) {
        if (elem.size() >= 2 && elem[0] == '"') {
            result.push_back(unescapeString(elem.substr(1, elem.size() - 2)));
        }
    }
    return result;
}

std::vector<int> JsonReader::getIntArray(const std::string& key) const {
    std::vector<int> result;
    std::string arrayStr = extractValue(key);
    auto elements = extractArrayElements(arrayStr);

    for (const auto& elem : elements) {
        try {
            result.push_back(std::stoi(elem));
        } catch (...) {
            result.push_back(0);
        }
    }
    return result;
}

std::vector<float> JsonReader::getFloatArray(const std::string& key) const {
    std::string arrayStr = extractValue(key);
    return parseFloatArray(arrayStr);
}

std::vector<JsonReader> JsonReader::getArray(const std::string& key) const {
    std::vector<JsonReader> result;
    std::string arrayStr = extractValue(key);
    auto elements = extractArrayElements(arrayStr);

    for (const auto& elem : elements) {
        if (!elem.empty() && (elem[0] == '{' || elem[0] == '[')) {
            result.emplace_back(elem);
        }
    }
    return result;
}

JsonReader JsonReader::getObject(const std::string& key) const {
    std::string value = extractValue(key);
    if (!value.empty() && value[0] == '{') {
        return JsonReader(value);
    }
    return JsonReader();
}

} // namespace MiEngine
