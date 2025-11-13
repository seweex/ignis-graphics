
#include <iostream>

#include <ignis/detail/include_vulkan_allocator.hxx>
#include <ignis/graphics/window.hxx>
#include <ignis/graphics/core.hxx>
#include <ignis/graphics/buffer.hxx>
#include <ignis/graphics/render_pass.hxx>

int main ()
{
    using namespace Ignis;

    SoftwareInfo constexpr app { "App", 0, 0, 1 };
    SoftwareInfo constexpr eng { "Eng", 0, 0, 1 };

    Graphics::Window window { 640, 480, "Test", Graphics::WindowType::unresizable };

    auto const core = std::make_shared <Graphics::Core> (window, app, eng);

    Graphics::RenderPassFactory <false> factory { core };

    auto renderPass =
        factory.build_render_pass()
        .begin_subpass("Default")
            .color_attachment (32)
            .msaa_attachment ()
        .end_subpass()
        .begin_subpass ("Depth")
            .depth_attachment ()
            .depend_on ("Default")
        .end_subpass()
        .confirm();

    for (;;);

    return 0;
}