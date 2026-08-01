#pragma once

#include "henia/gfx/Types.h"

#include <algorithm>
#include <cmath>

namespace henia::gfx {

[[nodiscard]] inline Mat4 multiply(const Mat4& left, const Mat4& right) noexcept {
    Mat4 result{{}};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0F;
            for (int index = 0; index < 4; ++index) {
                value += left.values[static_cast<std::size_t>(index * 4 + row)]
                    * right.values[static_cast<std::size_t>(column * 4 + index)];
            }
            result.values[static_cast<std::size_t>(column * 4 + row)] = value;
        }
    }
    return result;
}

[[nodiscard]] inline Mat4 perspective(
    float verticalFieldOfViewRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane) noexcept {
    const float tangent = std::tan(verticalFieldOfViewRadians * 0.5F);
    const float y = tangent > 0.0F ? 1.0F / tangent : 1.0F;
    const float x = y / std::max(aspectRatio, 0.0001F);
    const float range = farPlane - nearPlane;
    return {{
        x, 0.0F, 0.0F, 0.0F,
        0.0F, y, 0.0F, 0.0F,
        0.0F, 0.0F, farPlane / range, 1.0F,
        0.0F, 0.0F, -(nearPlane * farPlane) / range, 0.0F,
    }};
}

[[nodiscard]] inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept {
    const auto subtract = [](Vec3 a, Vec3 b) noexcept {
        return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
    };
    const auto dot = [](Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const auto normalize = [&](Vec3 value) noexcept {
        const float length = std::sqrt(dot(value, value));
        return length > 0.00001F
            ? Vec3{value.x / length, value.y / length, value.z / length}
            : Vec3{};
    };
    const auto cross = [](Vec3 a, Vec3 b) noexcept {
        return Vec3{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    };

    const Vec3 forward = normalize(subtract(target, eye));
    const Vec3 right = normalize(cross(up, forward));
    const Vec3 cameraUp = cross(forward, right);
    return {{
        right.x, cameraUp.x, forward.x, 0.0F,
        right.y, cameraUp.y, forward.y, 0.0F,
        right.z, cameraUp.z, forward.z, 0.0F,
        -dot(right, eye), -dot(cameraUp, eye), -dot(forward, eye), 1.0F,
    }};
}

} // namespace henia::gfx
