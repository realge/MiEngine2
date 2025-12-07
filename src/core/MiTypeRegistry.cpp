#include "core/MiTypeRegistry.h"
#include "core/MiObject.h"

namespace MiEngine {

MiTypeRegistry& MiTypeRegistry::getInstance() {
    static MiTypeRegistry instance;
    return instance;
}

std::shared_ptr<MiObject> MiTypeRegistry::create(const std::string& typeName) const {
    auto it = m_TypesByName.find(typeName);
    if (it != m_TypesByName.end() && it->second.factory) {
        return it->second.factory();
    }
    return nullptr;
}

std::shared_ptr<MiObject> MiTypeRegistry::createById(uint32_t typeId) const {
    auto it = m_TypesById.find(typeId);
    if (it != m_TypesById.end() && it->second && it->second->factory) {
        return it->second->factory();
    }
    return nullptr;
}

bool MiTypeRegistry::isRegistered(const std::string& typeName) const {
    return m_TypesByName.find(typeName) != m_TypesByName.end();
}

bool MiTypeRegistry::isRegisteredById(uint32_t typeId) const {
    return m_TypesById.find(typeId) != m_TypesById.end();
}

const TypeInfo* MiTypeRegistry::getTypeInfo(const std::string& typeName) const {
    auto it = m_TypesByName.find(typeName);
    if (it != m_TypesByName.end()) {
        return &it->second;
    }
    return nullptr;
}

const TypeInfo* MiTypeRegistry::getTypeInfoById(uint32_t typeId) const {
    auto it = m_TypesById.find(typeId);
    if (it != m_TypesById.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> MiTypeRegistry::getRegisteredTypeNames() const {
    std::vector<std::string> result;
    result.reserve(m_TypesByName.size());
    for (const auto& pair : m_TypesByName) {
        result.push_back(pair.first);
    }
    return result;
}

std::vector<std::string> MiTypeRegistry::getTypesDerivingFrom(uint32_t parentTypeId) const {
    std::vector<std::string> result;
    for (const auto& pair : m_TypesByName) {
        if (isDerivedFrom(pair.second.typeId, parentTypeId)) {
            result.push_back(pair.first);
        }
    }
    return result;
}

bool MiTypeRegistry::isDerivedFrom(uint32_t typeId, uint32_t parentTypeId) const {
    if (typeId == parentTypeId) {
        return true;
    }

    auto it = m_TypesById.find(typeId);
    if (it == m_TypesById.end() || !it->second) {
        return false;
    }

    uint32_t currentParent = it->second->parentTypeId;
    while (currentParent != 0) {
        if (currentParent == parentTypeId) {
            return true;
        }
        auto parentIt = m_TypesById.find(currentParent);
        if (parentIt == m_TypesById.end() || !parentIt->second) {
            break;
        }
        currentParent = parentIt->second->parentTypeId;
    }

    return false;
}

} // namespace MiEngine
