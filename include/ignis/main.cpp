
#include <iostream>

#include <ignis/detail/include_vulkan_allocator.hxx>
#include <ignis/graphics/window.hxx>
#include <ignis/graphics/core.hxx>
#include <ignis/graphics/buffer.hxx>
#include <ignis/graphics/render_pass.hxx>
#include <ignis/detail/swapchain.hxx>

int main ()
{
    using namespace Ignis;

    SoftwareInfo constexpr app { "App", 0, 0, 1 };
    SoftwareInfo constexpr eng { "Eng", 0, 0, 1 };

    Graphics::Window window { 640, 480, "Test", Graphics::WindowType::unresizable };

    auto const core = std::make_shared <Graphics::Core> (window, app, eng);
    auto const allocator = std::make_shared <Detail::ResourceMemoryAllocator> (core);

    auto const swapchain = std::make_shared <Detail::Swapchain> (core, 3, true);
    auto const depth = std::make_shared <Detail::DepthManager <false>> (allocator, vk::Extent2D{ 640, 480 }, 3);

    Graphics::RenderPassFactory <false> factory { swapchain, depth };

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