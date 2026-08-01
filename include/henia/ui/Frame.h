#pragma once

#include "henia/ui/BatchCompiler.h"
#include "henia/ui/Canvas.h"

#include <cstddef>

namespace henia::ui {

class Frame final {
public:
    Frame();

    void reserve(std::size_t commandCapacity, std::size_t batchCapacity);
    [[nodiscard]] Canvas& begin() noexcept;
    [[nodiscard]] const RenderPacket& finish();

    [[nodiscard]] const DisplayList& displayList() const noexcept;
    [[nodiscard]] const RenderPacket& packet() const noexcept;

private:
    DisplayList mDisplayList;
    Canvas mCanvas;
    BatchCompiler mCompiler;
    RenderPacket mPacket;
    bool mRecording = false;
};

} // namespace henia::ui
