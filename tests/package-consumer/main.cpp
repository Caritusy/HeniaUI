#include <henia/gfx/ShapeBatch3D.h>
#include <henia/ui/Frame.h>

#include <cstdlib>

int main() {
    henia::ui::Frame frame;
    frame.reserve(4, 1);
    frame.begin().fillRect({{0.0F, 0.0F}, {8.0F, 8.0F}}, {});
    const henia::ui::RenderPacket packet = frame.finish();

    henia::gfx::ShapeBatch3D shapes;
    static_cast<void>(shapes.addBox({}));
    const henia::gfx::InstanceBatch boxes = shapes.snapshot();
    return packet.instances().size() == 1 && boxes.boxes().size() == 1
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
