#include "henia/ui/Frame.h"

namespace henia::ui {

Frame::Frame() : mCanvas(mDisplayList) {}

void Frame::reserve(std::size_t commandCapacity, std::size_t batchCapacity) {
    mDisplayList.reserve(commandCapacity);
    mPacket.reserve(commandCapacity, batchCapacity);
}

Canvas& Frame::begin() noexcept {
    mDisplayList.clear();
    mCanvas.reset();
    mRecording = true;
    return mCanvas;
}

const RenderPacket& Frame::finish() {
    if (mRecording) {
        mCompiler.compile(mDisplayList, mPacket);
        mRecording = false;
    }
    return mPacket;
}

const DisplayList& Frame::displayList() const noexcept { return mDisplayList; }

const RenderPacket& Frame::packet() const noexcept { return mPacket; }

} // namespace henia::ui
