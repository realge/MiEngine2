#include "core/MiObject.h"
#include "core/JsonIO.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace MiEngine {

// Generate UUID v4 format string
ObjectId generateObjectId() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t ab = dis(gen);
    uint64_t cd = dis(gen);

    // Set version to 4 (random UUID)
    ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    // Set variant to RFC 4122
    cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << ((ab >> 32) & 0xFFFFFFFF) << "-";
    ss << std::setw(4) << ((ab >> 16) & 0xFFFF) << "-";
    ss << std::setw(4) << (ab & 0xFFFF) << "-";
    ss << std::setw(4) << ((cd >> 48) & 0xFFFF) << "-";
    ss << std::setw(12) << (cd & 0xFFFFFFFFFFFFULL);

    return ss.str();
}

MiObject::MiObject()
    : m_ObjectId(generateObjectId())
    , m_Name("Object")
    , m_PendingDestroy(false)
    , m_Dirty(false)
{
}

void MiObject::serialize(JsonWriter& writer) const {
    writer.writeString("id", m_ObjectId);
    writer.writeString("name", m_Name);
    writer.writeString("type", getTypeName());
}

void MiObject::deserialize(const JsonReader& reader) {
    m_ObjectId = reader.getString("id", m_ObjectId);
    m_Name = reader.getString("name", m_Name);
    // Type is read by the factory, not here
}

} // namespace MiEngine
