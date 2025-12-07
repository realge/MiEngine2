#pragma once

#include <functional>
#include <vector>
#include <algorithm>
#include <memory>

namespace MiEngine {

// Handle for delegate binding (allows unbinding later)
using DelegateHandle = uint64_t;

// Invalid handle constant
constexpr DelegateHandle InvalidDelegateHandle = 0;

// Single-cast delegate (one function only)
template<typename... Args>
class MiSingleDelegate {
public:
    using FunctionType = std::function<void(Args...)>;

    MiSingleDelegate() = default;

    // Bind a function
    void bind(FunctionType func) {
        m_Function = std::move(func);
    }

    // Bind a member function
    template<typename T>
    void bind(T* object, void (T::*memberFunc)(Args...)) {
        m_Function = [object, memberFunc](Args... args) {
            (object->*memberFunc)(std::forward<Args>(args)...);
        };
    }

    // Unbind
    void unbind() {
        m_Function = nullptr;
    }

    // Check if bound
    bool isBound() const {
        return static_cast<bool>(m_Function);
    }

    // Execute
    void execute(Args... args) const {
        if (m_Function) {
            m_Function(std::forward<Args>(args)...);
        }
    }

    // Execute if bound
    bool executeIfBound(Args... args) const {
        if (m_Function) {
            m_Function(std::forward<Args>(args)...);
            return true;
        }
        return false;
    }

    // Call operator
    void operator()(Args... args) const {
        execute(std::forward<Args>(args)...);
    }

private:
    FunctionType m_Function;
};

// Multi-cast delegate (multiple functions)
template<typename... Args>
class MiMulticastDelegate {
public:
    using FunctionType = std::function<void(Args...)>;

    MiMulticastDelegate() : m_NextHandle(1) {}

    // Add a function, returns handle for later removal
    DelegateHandle add(FunctionType func) {
        DelegateHandle handle = m_NextHandle++;
        m_Bindings.push_back({handle, std::move(func)});
        return handle;
    }

    // Add a member function
    template<typename T>
    DelegateHandle add(T* object, void (T::*memberFunc)(Args...)) {
        return add([object, memberFunc](Args... args) {
            (object->*memberFunc)(std::forward<Args>(args)...);
        });
    }

    // Add a lambda or functor
    template<typename F>
    DelegateHandle add(F&& functor) {
        return add(FunctionType(std::forward<F>(functor)));
    }

    // Remove by handle
    bool remove(DelegateHandle handle) {
        auto it = std::find_if(m_Bindings.begin(), m_Bindings.end(),
            [handle](const Binding& b) { return b.handle == handle; });

        if (it != m_Bindings.end()) {
            m_Bindings.erase(it);
            return true;
        }
        return false;
    }

    // Remove all bindings
    void clear() {
        m_Bindings.clear();
    }

    // Check if any bindings
    bool isBound() const {
        return !m_Bindings.empty();
    }

    // Get binding count
    size_t getBindingCount() const {
        return m_Bindings.size();
    }

    // Broadcast to all bindings
    void broadcast(Args... args) const {
        // Copy bindings in case a callback modifies the list
        auto bindingsCopy = m_Bindings;
        for (const auto& binding : bindingsCopy) {
            if (binding.function) {
                binding.function(args...);
            }
        }
    }

    // Call operator (broadcasts)
    void operator()(Args... args) const {
        broadcast(std::forward<Args>(args)...);
    }

private:
    struct Binding {
        DelegateHandle handle;
        FunctionType function;
    };

    std::vector<Binding> m_Bindings;
    DelegateHandle m_NextHandle;
};

// Convenience aliases
template<typename... Args>
using MiDelegate = MiMulticastDelegate<Args...>;

// Event class - same as MiMulticastDelegate but with clearer naming
template<typename... Args>
using MiEvent = MiMulticastDelegate<Args...>;

// RAII helper for automatic delegate unbinding
class MiDelegateHandle {
public:
    MiDelegateHandle() : m_Handle(InvalidDelegateHandle), m_Unbinder(nullptr) {}

    template<typename... Args>
    MiDelegateHandle(MiMulticastDelegate<Args...>& delegate, DelegateHandle handle)
        : m_Handle(handle)
        , m_Unbinder([&delegate, handle]() { delegate.remove(handle); })
    {}

    ~MiDelegateHandle() {
        unbind();
    }

    // Move only
    MiDelegateHandle(MiDelegateHandle&& other) noexcept
        : m_Handle(other.m_Handle)
        , m_Unbinder(std::move(other.m_Unbinder))
    {
        other.m_Handle = InvalidDelegateHandle;
        other.m_Unbinder = nullptr;
    }

    MiDelegateHandle& operator=(MiDelegateHandle&& other) noexcept {
        if (this != &other) {
            unbind();
            m_Handle = other.m_Handle;
            m_Unbinder = std::move(other.m_Unbinder);
            other.m_Handle = InvalidDelegateHandle;
            other.m_Unbinder = nullptr;
        }
        return *this;
    }

    // No copy
    MiDelegateHandle(const MiDelegateHandle&) = delete;
    MiDelegateHandle& operator=(const MiDelegateHandle&) = delete;

    void unbind() {
        if (m_Handle != InvalidDelegateHandle && m_Unbinder) {
            m_Unbinder();
            m_Handle = InvalidDelegateHandle;
            m_Unbinder = nullptr;
        }
    }

    bool isValid() const { return m_Handle != InvalidDelegateHandle; }
    DelegateHandle getHandle() const { return m_Handle; }

private:
    DelegateHandle m_Handle;
    std::function<void()> m_Unbinder;
};

// Helper to create RAII delegate handle
template<typename... Args>
MiDelegateHandle bindDelegate(MiMulticastDelegate<Args...>& delegate,
                               typename MiMulticastDelegate<Args...>::FunctionType func) {
    DelegateHandle handle = delegate.add(std::move(func));
    return MiDelegateHandle(delegate, handle);
}

} // namespace MiEngine
