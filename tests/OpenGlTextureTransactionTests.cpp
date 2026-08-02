#include "OpenGlFailure.h"
#include "OpenGlTextureTransaction.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>

namespace {

using henia::ui::detail::OpenGlTextureState;
using henia::ui::detail::OpenGlTextureTransaction;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void verifyUploadFailureRollsBackAndRetries() {
    std::array textures{
        OpenGlTextureState{.object = 11, .revision = 3},
        OpenGlTextureState{.object = 12, .revision = 5},
    };
    std::array<std::uint32_t, 4> destroyed{};
    std::size_t destroyedCount = 0;
    std::uint32_t nextObject = 20;
    const auto create = [&]() noexcept { return nextObject++; };
    const auto destroy = [&](std::uint32_t object) noexcept {
        destroyed[destroyedCount++] = object;
    };
    henia::detail::FixedError diagnostic;

    OpenGlTextureTransaction failed(textures);
    if (!failed.stage(0, 1, 4, create, [](std::uint32_t) { return true; }, destroy)
        || failed.stage(
            1,
            2,
            6,
            create,
            [&](std::uint32_t) noexcept {
                henia::detail::assignGlFailure(
                    diagnostic,
                    "OpenGL texture pixel upload failed",
                    0x0505U,
                    "texture",
                    2);
                return false;
            },
            destroy)) {
        fail("Injected OpenGL upload error was not reported");
    }
    failed.rollback(destroy);
    if (textures[0].object != 11 || textures[0].revision != 3
        || textures[1].object != 12 || textures[1].revision != 5
        || destroyedCount != 2 || destroyed[0] != 21 || destroyed[1] != 20
        || diagnostic.view()
            != "OpenGL texture pixel upload failed (texture=2, GL error=0x0505)") {
        fail("Failed OpenGL texture synchronization did not roll back atomically");
    }

    OpenGlTextureTransaction retry(textures);
    if (!retry.stage(0, 1, 4, create, [](std::uint32_t) { return true; }, destroy)
        || !retry.stage(1, 2, 6, create, [](std::uint32_t) { return true; }, destroy)
        || retry.commit(destroy) != 2) {
        fail("OpenGL texture synchronization could not retry after rollback");
    }
    if (textures[0].object != 22 || textures[0].revision != 4
        || textures[1].object != 23 || textures[1].revision != 6
        || std::find(destroyed.begin(), destroyed.begin() + destroyedCount, 11U)
            == destroyed.begin() + destroyedCount
        || std::find(destroyed.begin(), destroyed.begin() + destroyedCount, 12U)
            == destroyed.begin() + destroyedCount) {
        fail("Successful retry did not atomically replace the previous textures");
    }
}

void verifyCreationFailurePreservesValidTexture() {
    std::array textures{OpenGlTextureState{.object = 7, .revision = 9}};
    bool destroyed = false;
    OpenGlTextureTransaction transaction(textures);
    if (transaction.stage(
            0,
            1,
            10,
            []() noexcept { return 0U; },
            [](std::uint32_t) noexcept { return true; },
            [&](std::uint32_t) noexcept { destroyed = true; })) {
        fail("Injected OpenGL object-creation failure was accepted");
    }
    transaction.rollback([&](std::uint32_t) noexcept { destroyed = true; });
    if (textures[0].object != 7 || textures[0].revision != 9 || destroyed) {
        fail("Creation failure changed a previously valid texture");
    }
}

} // namespace

int main() {
    verifyUploadFailureRollsBackAndRetries();
    verifyCreationFailurePreservesValidTexture();
    std::cout << "HeniaUI OpenGL texture transaction tests passed\n";
    return EXIT_SUCCESS;
}
