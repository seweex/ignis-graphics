
#include <iostream>

#include <ignis/graphics/window.hxx>
#include <ignis/graphics/core.hxx>
#include <ignis/graphics/buffer.hxx>

int main ()
{
    using namespace Ignis;

    SoftwareInfo constexpr app { "App", 0, 0, 1 };
    SoftwareInfo constexpr eng { "Eng", 0, 0, 1 };

    Graphics::Window window { 640, 480, "Test", Graphics::WindowType::unresizable };

    auto const core = std::make_shared <Graphics::Core> (window, app, eng);

    Graphics::BufferFactory <false> factory { core };

    (void) factory.make_buffer
        <Graphics::BufferType::constantly_mapped, Graphics::BufferUsage::vertex, Graphics::MemoryPlacement::host>
            (12ull * 1024 *1024 * 1024, false, false);

    (void) factory.make_buffer
        <Graphics::BufferType::transferable, Graphics::BufferUsage::storage, Graphics::MemoryPlacement::no_matter>
            (3ull * 1024 * 1024 * 1024, false, true);

    for (;;);

    return 0;
}