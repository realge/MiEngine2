#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <vector>

namespace MiEngine {

class MiObject;

// Factory function type for creating objects
using ObjectFactory = std::function<std::shared_ptr<MiObject>()>;

// Type metadata
struct TypeInfo {
    std::string typeName;
    uint32_t typeId;
    ObjectFactory factory;
    uint32_t parentTypeId;  // 0 if no parent
};

// Runtime type registry - singleton for creating objects by type name
class MiTypeRegistry {
public:
    // Get singleton instance
    static MiTypeRegistry& getInstance();

    // Register a type with factory
    template<typename T>
    void registerType();

    // Register with explicit parent type
    template<typename T, typename TParent>
    void registerTypeWithParent();

    // Create object by type name
    std::shared_ptr<MiObject> create(const std::string& typeName) const;

    // Create object by type ID
    std::shared_ptr<MiObject> createById(uint32_t typeId) const;

    // Check if type is registered
    bool isRegistered(const std::string& typeName) const;
    bool isRegisteredById(uint32_t typeId) const;

    // Get type info
    const TypeInfo* getTypeInfo(const std::string& typeName) const;
    const TypeInfo* getTypeInfoById(uint32_t typeId) const;

    // Get all registered type names
    std::vector<std::string> getRegisteredTypeNames() const;

    // Get all types that derive from a specific type
    std::vector<std::string> getTypesDerivingFrom(uint32_t parentTypeId) const;

    // Check if type A is derived from type B
    bool isDerivedFrom(uint32_t typeId, uint32_t parentTypeId) const;

private:
    MiTypeRegistry() = default;
    ~MiTypeRegistry() = default;

    // Non-copyable
    MiTypeRegistry(const MiTypeRegistry&) = delete;
    MiTypeRegistry& operator=(const MiTypeRegistry&) = delete;

    std::unordered_map<std::string, TypeInfo> m_TypesByName;
    std::unordered_map<uint32_t, TypeInfo*> m_TypesById;
};

// Template implementation
template<typename T>
void MiTypeRegistry::registerType() {
    TypeInfo info;
    info.typeName = T::StaticTypeName;
    info.typeId = T::StaticTypeId;
    info.factory = []() -> std::shared_ptr<MiObject> {
        return std::make_shared<T>();
    };
    info.parentTypeId = 0;

    m_TypesByName[info.typeName] = info;
    m_TypesById[info.typeId] = &m_TypesByName[info.typeName];
}

template<typename T, typename TParent>
void MiTypeRegistry::registerTypeWithParent() {
    TypeInfo info;
    info.typeName = T::StaticTypeName;
    info.typeId = T::StaticTypeId;
    info.factory = []() -> std::shared_ptr<MiObject> {
        return std::make_shared<T>();
    };
    info.parentTypeId = TParent::StaticTypeId;

    m_TypesByName[info.typeName] = info;
    m_TypesById[info.typeId] = &m_TypesByName[info.typeName];
}

// Helper macro for automatic type registration
// Use in .cpp file: MI_REGISTER_TYPE(MyClassName)
#define MI_REGISTER_TYPE(TypeName) \
    namespace { \
        struct TypeName##_AutoRegistrar { \
            TypeName##_AutoRegistrar() { \
                MiEngine::MiTypeRegistry::getInstance().registerType<TypeName>(); \
            } \
        }; \
        static TypeName##_AutoRegistrar s_##TypeName##_AutoRegistrar; \
    }

// Register with parent type
#define MI_REGISTER_TYPE_WITH_PARENT(TypeName, ParentTypeName) \
    namespace { \
        struct TypeName##_AutoRegistrar { \
            TypeName##_AutoRegistrar() { \
                MiEngine::MiTypeRegistry::getInstance().registerTypeWithParent<TypeName, ParentTypeName>(); \
            } \
        }; \
        static TypeName##_AutoRegistrar s_##TypeName##_AutoRegistrar; \
    }

} // namespace MiEngine
