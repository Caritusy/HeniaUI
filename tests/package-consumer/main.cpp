#include <henia/CheckedArithmetic.h>
#include <henia/gfx/Math.h>
#include <henia/gfx/ShapeBatch3D.h>
#include <henia/gfx/Validation.h>
#include <henia/ui/Frame.h>
#include <henia/ui/text/TextEditor.h>
#include <henia/ui/Validation.h>
#include <henia/ui/widget/controls/TextInput.h>

#include <cstdlib>
#include <string>

int main() {
    std::size_t bytes = 0;
    if (!henia::checkedMultiply<std::size_t>(4, 16, bytes) || bytes != 64) {
        return EXIT_FAILURE;
    }
    henia::ui::Frame frame;
    frame.reserve(4, 4);
    henia::ui::Canvas& canvas = frame.begin();
    canvas.fillRect({{0.0F, 0.0F}, {8.0F, 8.0F}}, {});
    canvas.circle({4.0F, 4.0F}, 2.0F, {});
    canvas.arc({{1.0F, 1.0F}, {7.0F, 7.0F}}, 0.0F, 3.14F, {}, 1.0F);
    canvas.border({{0.0F, 0.0F}, {8.0F, 8.0F}}, {}, {1.0F, 2.0F, 3.0F, 4.0F}, 1.0F);
    const henia::ui::RenderPacket packet = frame.finish();

    henia::ui::TextEditorState editor("A");
    static_cast<void>(editor.insert(U'\u4E2D'));
    henia::ui::TextInput textInput(std::string(editor.text()));

    henia::gfx::ShapeBatch3D shapes;
    static_cast<void>(shapes.addBox({}));
    const henia::gfx::InstanceBatch boxes = shapes.snapshot();
    henia::gfx::Mat4 projection{};
    henia::ui::ScissorRect scissor{};
    return packet.instances().size() == 4 && boxes.boxes().size() == 1
        && textInput.text() == "A\xE4\xB8\xAD"
        && henia::gfx::tryPerspective(1.0F, 1.0F, 0.1F, 100.0F, projection)
        && henia::gfx::finite(projection)
        && henia::ui::makeScissorRect({{0.25F, 0.25F}, {7.25F, 7.25F}}, 8, 8, scissor)
        && scissor.left == 0 && scissor.right == 8
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
