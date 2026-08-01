#pragma once

#include <utility>

namespace henia::ui {

template <typename... Arguments>
class Callback final {
public:
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
