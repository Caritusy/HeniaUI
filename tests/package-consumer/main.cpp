#include <henia/CheckedArithmetic.h>
#include <henia/gfx/Math.h>
#include <henia/gfx/ShapeBatch3D.h>
#include <henia/gfx/Validation.h>
#include <henia/ui/Frame.h>
#include <henia/ui/text/TextEditor.h>
#include <henia/ui/Validation.h>
#include <henia/ui/widget/controls/TextInput.h>
#include <henia/ui/widget/controls/ColorPicker.h>
#include <henia/ui/widget/controls/ComboBox.h>
#include <henia/ui/widget/controls/KeyBindingEditor.h>
#include <henia/ui/widget/controls/ListView.h>
#include <henia/ui/widget/controls/PopupLayer.h>
#include <henia/ui/widget/controls/ScrollContainer.h>
#include <henia/ui/widget/controls/Slider.h>
#include <henia/ui/widget/controls/TabBar.h>
#include <henia/ui/widget/controls/Toggle.h>
#include <henia/ui/widget/controls/Tooltip.h>
#include <henia/ui/widget/controls/TreeView.h>

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
    henia::ui::Checkbox checkbox("Enabled", true);
    henia::ui::Toggle toggle("Mode", true);
    henia::ui::Slider slider(0.5);
    henia::ui::ComboBox combo({"A", "B"});
    henia::ui::TabBar tabs({"One", "Two"});
    henia::ui::ListView list({"First", "Second"});
    henia::ui::TreeView tree({{"Root"}});
    henia::ui::ColorPicker picker;
    henia::ui::KeyBindingEditor binding(henia::ui::KeyCode::F1);
    henia::ui::Tooltip tooltip("Tip");
    henia::ui::ScrollContainer scroll;
    henia::ui::PopupLayer popup;

    henia::gfx::ShapeBatch3D shapes;
    static_cast<void>(shapes.addBox({}));
    const henia::gfx::InstanceBatch boxes = shapes.snapshot();
    henia::gfx::Mat4 projection{};
    henia::ui::ScissorRect scissor{};
    return packet.instances().size() == 4 && boxes.boxes().size() == 1
        && textInput.text() == "A\xE4\xB8\xAD"
        && checkbox.checked() && toggle.checked() && slider.value() == 0.5
        && combo.itemCount() == 2 && tabs.tabCount() == 2 && list.itemCount() == 2
        && tree.nodeCount() == 1 && picker.color().alpha == 1.0F
        && binding.binding() == henia::ui::KeyCode::F1 && tooltip.text() == "Tip"
        && scroll.content() == nullptr && popup.popup() == nullptr
        && henia::gfx::tryPerspective(1.0F, 1.0F, 0.1F, 100.0F, projection)
        && henia::gfx::finite(projection)
        && henia::ui::makeScissorRect({{0.25F, 0.25F}, {7.25F, 7.25F}}, 8, 8, scissor)
        && scissor.left == 0 && scissor.right == 8
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
