#pragma once

#include <utility>

namespace henia::ui {

template <typename Result, typename... Arguments>
class ValueCallback final {
public:
    using Invoker = Result (*)(void*, Arguments...);

    constexpr ValueCallback() noexcept = default;
    constexpr ValueCallback(void* context, Invoker invoker) noexcept
        : mContext(context), mInvoker(invoker) {}

    template <typename Object, Result (Object::*Method)(Arguments...)>
    [[nodiscard]] static constexpr ValueCallback bind(Object& object) noexcept {
        return {
            &object,
            [](void* context, Arguments... arguments) -> Result {
                return (static_cast<Object*>(context)->*Method)(
                    std::forward<Arguments>(arguments)...);
            },
        };
    }

    template <typename Object, Result (Object::*Method)(Arguments...) const>
    [[nodiscard]] static constexpr ValueCallback bind(const Object& object) noexcept {
        return {
            const_cast<Object*>(&object),
            [](void* context, Arguments... arguments) -> Result {
                return (static_cast<const Object*>(context)->*Method)(
                    std::forward<Arguments>(arguments)...);
            },
        };
    }

    [[nodiscard]] Result operator()(Arguments... arguments) const {
        return mInvoker == nullptr
            ? Result{}
            : mInvoker(mContext, std::forward<Arguments>(arguments)...);
    }

    [[nodiscard]] constexpr bool valid() const noexcept { return mInvoker != nullptr; }
    explicit constexpr operator bool() const noexcept { return valid(); }

private:
    void* mContext = nullptr;
    Invoker mInvoker = nullptr;
};

template <typename... Arguments>
class Callback final {
public:
    // Callback exceptions propagate to the dispatch caller. Hosts that require
    // a non-throwing boundary must catch there; HeniaUI never terminates them.
    using Invoker = void (*)(void*, Arguments...);

    constexpr Callback() noexcept = default;
    constexpr Callback(void* context, Invoker invoker) noexcept
        : mContext(context), mInvoker(invoker) {}

    template <typename Object, void (Object::*Method)(Arguments...)>
    [[nodiscard]] static constexpr Callback bind(Object& object) noexcept {
        return {
            &object,
            [](void* context, Arguments... arguments) {
                (static_cast<Object*>(context)->*Method)(std::forward<Arguments>(arguments)...);
            },
        };
    }

    void operator()(Arguments... arguments) const {
        if (mInvoker != nullptr) {
            mInvoker(mContext, std::forward<Arguments>(arguments)...);
        }
    }

    [[nodiscard]] constexpr bool valid() const noexcept { return mInvoker != nullptr; }
    explicit constexpr operator bool() const noexcept { return valid(); }

private:
    void* mContext = nullptr;
    Invoker mInvoker = nullptr;
};

} // namespace henia::ui
