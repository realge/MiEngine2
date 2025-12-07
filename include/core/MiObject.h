#pragma once

#include <string>
#include <cstdint>
#include <memory>

namespace MiEngine {

// Forward declarations
class JsonWriter;
class JsonReader;
class MiWorld;

// Unique identifier type (UUID as string)
using ObjectId = std::string;

// Generate a new unique ID (UUID v4 format)
ObjectId generateObjectId();

// Base class for all engine objects (similar to UObject in UE5)
class MiObject : public std::enable_shared_from_this<MiObject> {
public:
    MiObject();
    virtual ~MiObject() = default;

    // Prevent copying
    MiObject(const MiObject&) = delete;
    MiObject& operator=(const MiObject&) = delete;

    // Allow moving
    MiObject(MiObject&&) = default;
    MiObject& operator=(MiObject&&) = default;

    // Unique identifier (persists across save/load)
    const ObjectId& getObjectId() const { return m_ObjectId; }
    void setObjectId(const ObjectId& id) { m_ObjectId = id; }

    // Display name
    const std::string& getName() const { return m_Name; }
    void setName(const std::string& name) { m_Name = name; }

    // Runtime type information
    virtual const char* getTypeName() const = 0;
    virtual uint32_t getTypeId() const = 0;

    // Check if this object is of a specific type
    template<typename T>
    bool isA() const {
        return getTypeId() == T::StaticTypeId;
    }

    // Serialization (override in derived classes)
    virtual void serialize(JsonWriter& writer) const;
    virtual void deserialize(const JsonReader& reader);

    // Lifecycle callbacks
    virtual void onCreated() {}      // Called after construction and registration
    virtual void onDestroyed() {}    // Called before destruction

    // Destruction state
    bool isPendingDestroy() const { return m_PendingDestroy; }
    void markPendingDestroy() { m_PendingDestroy = true; }

    // Dirty flag (for editor/save tracking)
    bool isDirty() const { return m_Dirty; }
    virtual void markDirty() { m_Dirty = true; }
    virtual void clearDirty() { m_Dirty = false; }

protected:
    ObjectId m_ObjectId;
    std::string m_Name = "Object";
    bool m_PendingDestroy = false;
    bool m_Dirty = false;
};

// Macro for declaring type information in derived classes
// Usage: MI_OBJECT_BODY(MyClassName, UniqueTypeId)
#define MI_OBJECT_BODY(TypeName, TypeIdValue) \
public: \
    static constexpr const char* StaticTypeName = #TypeName; \
    static constexpr uint32_t StaticTypeId = TypeIdValue; \
    virtual const char* getTypeName() const override { return StaticTypeName; } \
    virtual uint32_t getTypeId() const override { return StaticTypeId; } \
private:

} // namespace MiEngine
