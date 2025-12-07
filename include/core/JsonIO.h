#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace MiEngine {

// JSON writer for serialization
class JsonWriter {
public:
    JsonWriter();

    // Object/Array structure
    void beginObject();
    void beginObject(const std::string& key);
    void endObject();
    void beginArray(const std::string& key);
    void endArray();

    // Write primitives
    void writeString(const std::string& key, const std::string& value);
    void writeInt(const std::string& key, int value);
    void writeUInt(const std::string& key, uint32_t value);
    void writeUInt64(const std::string& key, uint64_t value);
    void writeFloat(const std::string& key, float value);
    void writeDouble(const std::string& key, double value);
    void writeBool(const std::string& key, bool value);
    void writeNull(const std::string& key);

    // Write GLM types
    void writeVec2(const std::string& key, const glm::vec2& value);
    void writeVec3(const std::string& key, const glm::vec3& value);
    void writeVec4(const std::string& key, const glm::vec4& value);
    void writeQuat(const std::string& key, const glm::quat& value);
    void writeMat4(const std::string& key, const glm::mat4& value);

    // Write array elements (for use within beginArray/endArray)
    void writeArrayString(const std::string& value);
    void writeArrayInt(int value);
    void writeArrayFloat(float value);
    void beginArrayObject();

    // Output
    std::string toString() const;
    bool saveToFile(const std::string& filePath) const;

private:
    std::ostringstream m_Stream;
    int m_Indent = 0;
    bool m_NeedComma = false;
    std::vector<bool> m_InArrayStack;

    void writeIndent();
    void writeCommaIfNeeded();
    void writeKey(const std::string& key);
    static std::string escapeString(const std::string& str);
};

// JSON reader for deserialization
class JsonReader {
public:
    JsonReader();
    explicit JsonReader(const std::string& json);

    // Load from file or string
    bool loadFromFile(const std::string& filePath);
    bool loadFromString(const std::string& json);

    // Check if key exists
    bool hasKey(const std::string& key) const;

    // Read primitives
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    uint32_t getUInt(const std::string& key, uint32_t defaultValue = 0) const;
    uint64_t getUInt64(const std::string& key, uint64_t defaultValue = 0) const;
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;
    double getDouble(const std::string& key, double defaultValue = 0.0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;

    // Read GLM types
    glm::vec2 getVec2(const std::string& key, const glm::vec2& defaultValue = glm::vec2(0.0f)) const;
    glm::vec3 getVec3(const std::string& key, const glm::vec3& defaultValue = glm::vec3(0.0f)) const;
    glm::vec4 getVec4(const std::string& key, const glm::vec4& defaultValue = glm::vec4(0.0f)) const;
    glm::quat getQuat(const std::string& key, const glm::quat& defaultValue = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) const;
    glm::mat4 getMat4(const std::string& key, const glm::mat4& defaultValue = glm::mat4(1.0f)) const;

    // Read arrays
    std::vector<std::string> getStringArray(const std::string& key) const;
    std::vector<int> getIntArray(const std::string& key) const;
    std::vector<float> getFloatArray(const std::string& key) const;

    // Read nested objects/arrays
    std::vector<JsonReader> getArray(const std::string& key) const;
    JsonReader getObject(const std::string& key) const;

    // Get raw JSON string
    const std::string& getRawJson() const { return m_Json; }
    bool isValid() const { return !m_Json.empty(); }

private:
    std::string m_Json;

    std::string extractValue(const std::string& key) const;
    std::vector<std::string> extractArrayElements(const std::string& arrayStr) const;
    std::vector<float> parseFloatArray(const std::string& arrayStr) const;
    static std::string unescapeString(const std::string& str);
    static std::string trim(const std::string& str);
};

} // namespace MiEngine
