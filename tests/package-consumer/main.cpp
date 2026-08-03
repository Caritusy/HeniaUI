#include <henia/CheckedArithmetic.h>
#include <henia/gfx/Math.h>
#include <henia/gfx/ShapeBatch3D.h>
#include <henia/gfx/Validation.h>
#include <henia/gfx/VisibilityList.h>
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
#include <array>
#include <memory>
#include <string>

namespace {

struct PackageListModel final {
    [[nodiscard]] henia::ui::ListItemKey key(std::size_t index) const noexcept {
        return 100U + static_cast<henia::ui::ListItemKey>(index);
    }
    [[nodiscard]] float extent(std::size_t index) const noexcept {
        return index % 2U == 0 ? 20.0F : 24.0F;
    }
    [[nodiscard]] std::unique_ptr<henia::ui::Widget> create() {
        return std::make_unique<henia::ui::Widget>();
    }
    void bind(
        henia::ui::Widget&,
        std::size_t,
        henia::ui::ListItemKey,
        bool) {}
};

} // namespace

int main() {
    std::size_t bytes = 0;
    if (!henia::checkedMultiply<std::size_t>(4, 16, bytes) || bytes != 64) {
        return EXIT_FAILURE;
    }
    henia::ui::Frame frame;
    frame.reserve(8, 8);
    henia::ui::Canvas& canvas = frame.begin();
    canvas.fillRect({{0.0F, 0.0F}, {8.0F, 8.0F}}, {});
    canvas.circle({4.0F, 4.0F}, 2.0F, {});
    canvas.arc({{1.0F, 1.0F}, {7.0F, 7.0F}}, 0.0F, 3.14F, {}, 1.0F);
    canvas.border({{0.0F, 0.0F}, {8.0F, 8.0F}}, {}, {1.0F, 2.0F, 3.0F, 4.0F}, 1.0F);
    const std::array effects{
        henia::ui::EffectLayer{
            .kind = henia::ui::EffectLayerKind::AnimatedGradient,
            .color = {0.2F, 0.6F, 1.0F, 1.0F},
            .secondaryColor = {0.8F, 0.2F, 0.7F, 1.0F},
            .phase = 0.25F,
        },
        henia::ui::EffectLayer{
            .kind = henia::ui::EffectLayerKind::Outline,
            .amount = 1.0F,
        },
        henia::ui::EffectLayer{.enabled = false},
    };
    canvas.effectRect({{10.0F, 0.0F}, {18.0F, 8.0F}}, 2.0F, effects);
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
    PackageListModel listModel;
    henia::ui::ListView recycledList;
    recycledList.setRecycledItems({
        .itemCount = 50'000,
        .itemKey = henia::ui::ValueCallback<henia::ui::ListItemKey, std::size_t>::bind<
            PackageListModel, &PackageListModel::key>(listModel),
        .itemExtent = henia::ui::ValueCallback<float, std::size_t>::bind<
            PackageListModel, &PackageListModel::extent>(listModel),
        .createWidget = henia::ui::ValueCallback<std::unique_ptr<henia::ui::Widget>>::bind<
            PackageListModel, &PackageListModel::create>(listModel),
        .bindWidget = henia::ui::Callback<
            henia::ui::Widget&, std::size_t, henia::ui::ListItemKey, bool>::bind<
                PackageListModel, &PackageListModel::bind>(listModel),
    });
    recycledList.setSelectedItemKey(42'100);
    henia::ui::TreeView tree({{"Root"}});
    henia::ui::ColorPicker picker;
    henia::ui::KeyBindingEditor binding(henia::ui::KeyCode::F1);
    henia::ui::Tooltip tooltip("Tip");
    henia::ui::ScrollContainer scroll;
    henia::ui::PopupLayer popup;

    henia::gfx::ShapeBatch3D shapes;
    static_cast<void>(shapes.addBox({}));
    const henia::gfx::InstanceBatch boxes = shapes.snapshot();
    henia::gfx::VisibilityList visibility;
    const henia::gfx::ViewParameters visibilityView{.viewport = {8.0F, 8.0F}};
    henia::gfx::Mat4 projection{};
    henia::ui::ScissorRect scissor{};
    return packet.instances().size() == 6 && packet.statistics().effectInstances == 2
        && boxes.boxes().size() == 1
        && visibility.reserve(1) && visibility.update(
            boxes,
            visibilityView,
            {.mode = henia::gfx::VisibilityMode::CpuFrustum})
        && visibility.boxes().size() == 1
        && textInput.text() == "A\xE4\xB8\xAD"
        && checkbox.checked() && toggle.checked() && slider.value() == 0.5
        && combo.itemCount() == 2 && tabs.tabCount() == 2 && list.itemCount() == 2
        && recycledList.itemCount() == 50'000
        && recycledList.selectedItemKey() == 42'100
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
