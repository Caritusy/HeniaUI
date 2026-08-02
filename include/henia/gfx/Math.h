#pragma once

#include "henia/gfx/Types.h"
#include "henia/gfx/Validation.h"

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
    return finite(result) ? result : Mat4{};
}

[[nodiscard]] inline bool tryPerspective(
    float verticalFieldOfViewRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane,
    Mat4& output) noexcept {
    constexpr float pi = 3.14159265358979323846F;
    if (!std::isfinite(verticalFieldOfViewRadians) || verticalFieldOfViewRadians <= 0.0F
        || verticalFieldOfViewRadians >= pi || !std::isfinite(aspectRatio) || aspectRatio <= 0.0F
        || !std::isfinite(nearPlane) || nearPlane <= 0.0F
        || !std::isfinite(farPlane) || farPlane <= nearPlane) {
        output = {};
        return false;
    }
    const float tangent = std::tan(verticalFieldOfViewRadians * 0.5F);
    const float y = 1.0F / tangent;
    const float x = y / aspectRatio;
    const float range = farPlane - nearPlane;
    output = {{
        x, 0.0F, 0.0F, 0.0F,
        0.0F, y, 0.0F, 0.0F,
        0.0F, 0.0F, farPlane / range, 1.0F,
        0.0F, 0.0F, -(nearPlane * farPlane) / range, 0.0F,
    }};
    if (!finite(output)) {
        output = {};
        return false;
    }
    return true;
}

[[nodiscard]] inline Mat4 perspective(
    float verticalFieldOfViewRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane) noexcept {
    Mat4 result{};
    static_cast<void>(tryPerspective(
        verticalFieldOfViewRadians, aspectRatio, nearPlane, farPlane, result));
    return result;
}

[[nodiscard]] inline bool tryLookAt(Vec3 eye, Vec3 target, Vec3 up, Mat4& output) noexcept {
    if (!finite(eye) || !finite(target) || !finite(up)) {
        output = {};
        return false;
    }
    const auto subtract = [](Vec3 a, Vec3 b) noexcept {
        return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
    };
    const auto dot = [](Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const auto cross = [](Vec3 a, Vec3 b) noexcept {
        return Vec3{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    };
    const auto normalize = [&](Vec3 value, Vec3& normalized) noexcept {
        const float squaredLength = dot(value, value);
        if (!std::isfinite(squaredLength) || squaredLength <= 0.0000000001F) return false;
        const float inverseLength = 1.0F / std::sqrt(squaredLength);
        normalized = {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
        return finite(normalized);
    };
    Vec3 forward{};
    Vec3 right{};
    if (!normalize(subtract(target, eye), forward) || !normalize(cross(up, forward), right)) {
        output = {};
        return false;
    }
    const Vec3 cameraUp = cross(forward, right);
    output = {{
        right.x, cameraUp.x, forward.x, 0.0F,
        right.y, cameraUp.y, forward.y, 0.0F,
        right.z, cameraUp.z, forward.z, 0.0F,
        -dot(right, eye), -dot(cameraUp, eye), -dot(forward, eye), 1.0F,
    }};
    if (!finite(output)) {
        output = {};
        return false;
    }
    return true;
}

[[nodiscard]] inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept {
    Mat4 result{};
    static_cast<void>(tryLookAt(eye, target, up, result));
    return result;
}

} // namespace henia::gfx
