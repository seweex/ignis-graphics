#ifndef IGNIS_GRAPHICS_WINDOW_HXX
#define IGNIS_GRAPHICS_WINDOW_HXX

#include <cassert>
#include <stdexcept>
#include <vector>
#include <string_view>

#include <ignis/detail/include_glfw.hxx>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/glfw_dependent.hxx>
#include <ignis/input/event.hxx>

namespace Ignis::Input
{
    class Map;
}

namespace Ignis::Graphics
{
    enum class WindowType
        { resizable, unresizable, fullscreen };
}

namespace Ignis::Detail
{
    class GLFWWindow :
        protected virtual GLFWDependent
    {
        [[nodiscard]] static GLFWwindow*
        make_window (uint32_t const width, uint32_t const height,
            std::string_view const title, Graphics::WindowType const type)
        {
            assert (width != 0);
            assert (height != 0);
            assert (!title.empty());

            GLFWmonitor* const monitor = type == Graphics::WindowType::fullscreen ?
                glfwGetPrimaryMonitor() : nullptr;

            glfwWindowHint (GLFW_RESIZABLE, type == Graphics::WindowType::resizable);

            auto const window = glfwCreateWindow (static_cast<int>(width),
                static_cast<int>(height), title.data(), monitor, nullptr);

            if (!window) [[unlikely]]
                throw std::runtime_error("Failed to create a window");

            return window;
        }

        void set_window_state (bool const shown) noexcept
        {
            GLFWDependent::assert_context_creation_thread();
            assert (myHandle);

            shown ? glfwShowWindow (myHandle) :
                glfwHideWindow (myHandle);
        }

        void set_cursor_state (bool const locked) noexcept
        {
            GLFWDependent::assert_context_creation_thread();
            assert (myHandle);

            glfwSetInputMode (myHandle, GLFW_CURSOR,
                locked ? GLFW_CURSOR_DISABLED: GLFW_CURSOR_NORMAL);
        }

    protected:
        GLFWWindow (uint32_t const width, uint32_t const height,
            std::string_view const title, Graphics::WindowType const type)
        :
            myHandle (make_window (width, height, title, type))
        {}

    public:
        GLFWWindow (GLFWWindow const&) = delete;
        GLFWWindow& operator=(GLFWWindow const&) = delete;

        GLFWWindow (GLFWWindow&& other) noexcept :
            myHandle (std::exchange(other.myHandle, nullptr))
        {}

        GLFWWindow& operator=(GLFWWindow&& other) noexcept {
            myHandle = std::exchange(other.myHandle, nullptr);
            return *this;
        }

        ~GLFWWindow() noexcept {
            if (myHandle)
                glfwDestroyWindow (myHandle);
        }

        [[nodiscard]] bool
        closing() const noexcept {
            assert (myHandle);
            return glfwWindowShouldClose (myHandle);
        }

        [[nodiscard]] std::pair <uint32_t, uint32_t>
        size() const noexcept
        {
            GLFWDependent::assert_context_creation_thread();
            assert (myHandle);

            int width, height;
            glfwGetWindowSize (myHandle, &width, &height);

            return {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height),
            };
        }

        void lock_cursor () noexcept {
            set_cursor_state (true);
        }

        void release_cursor () noexcept {
            set_cursor_state (false);
        }

        void show () noexcept {
            set_window_state(true);
        }

        void hide () noexcept {
            set_window_state(false);
        }

    protected:
        GLFWwindow* myHandle;
    };

    class WindowSurface :
        protected virtual GLFWDependent
    {
    protected:
        WindowSurface () noexcept :
            mySurface (VK_NULL_HANDLE)
        {}

    public:
        WindowSurface (WindowSurface const&) = delete;
        WindowSurface& operator=(WindowSurface const&) = delete;

    protected:
        [[nodiscard]] vk::raii::SurfaceKHR
        create_surface (vk::raii::Instance const& instance, GLFWwindow* const window)
        {
            GLFWDependent::assert_context_creation_thread();

            assert (window);
            assert (instance != VK_NULL_HANDLE);
            assert (mySurface == VK_NULL_HANDLE);

            VkSurfaceKHR surface;

            auto const result = glfwCreateWindowSurface
                (*instance, window, nullptr, &surface);

            if (result != VK_SUCCESS) [[unlikely]]
                throw std::runtime_error ("Failed to create window surface");

            vk::raii::SurfaceKHR surfaceHandle { instance, surface };
            mySurface = surfaceHandle;

            return surfaceHandle;
        }

    private:
        vk::SurfaceKHR mySurface;
    };

    class WindowInput :
        protected virtual GLFWDependent
    {
        class Callbacks final
        {
            [[nodiscard]] static WindowInput&
            extract_window (GLFWwindow* const handle) noexcept
            {
                auto const userPointer = glfwGetWindowUserPointer(handle);
                assert (userPointer);

                return *static_cast <WindowInput*> (userPointer);
            }

        public:
            static void key (GLFWwindow* const handle, int const key,
                [[maybe_unused]] int const scancode, int const action, [[maybe_unused]] int const mods)
            {
                auto& window = extract_window(handle);

                switch (action)
                {
                case GLFW_REPEAT: [[fallthrough]];
                case GLFW_PRESS:
                    window.myButtonEvents.emplace_back (static_cast <Input::Key> (key), Input::Action::pressed);
                    break;

                case GLFW_RELEASE:
                    window.myButtonEvents.emplace_back (static_cast <Input::Key> (key), Input::Action::released);
                    break;

                default:
                    break;
                }
            }

            static void cursor (GLFWwindow* const handle, double const x, double const y)
            {
                auto& window = extract_window(handle);

                window.myMouseState.cursorX = x;
                window.myMouseState.cursorY = y;
            }

            static void scroll (GLFWwindow* const handle, double const x, double const y)
            {
                auto& window = extract_window(handle);

                window.myMouseState.scrollX = x;
                window.myMouseState.scrollY = y;
            }
        };

        friend class Callbacks;

    protected:
        WindowInput() noexcept :
            myMouseState (0, 0, 0, 0)
        {}

        void register_callbacks (GLFWwindow* handle) noexcept
        {
            glfwSetWindowUserPointer (handle, this);

            glfwSetKeyCallback (handle, &Callbacks::key);
            glfwSetCursorPosCallback (handle, &Callbacks::cursor);
            glfwSetScrollCallback (handle, &Callbacks::scroll);
        }

    public:
        static void poll_all_events() {
            GLFWDependent::assert_context_creation_thread();
            glfwPollEvents();
        }

    private:
        [[nodiscard]] Input::MouseState
        get_mouse_state () const noexcept {
            return myMouseState;
        }

        void swap_events_buffers (std::vector <Input::ButtonEvent>& other) noexcept {
            myButtonEvents.swap (other);
        }

        friend class Input::Map;

        std::vector <Input::ButtonEvent> myButtonEvents;
        Input::MouseState myMouseState;
    };
}

namespace Ignis::Graphics
{
    class Window final :
        public Detail::GLFWWindow,
        public Detail::WindowSurface,
        public Detail::WindowInput
    {
    public:
        Window (uint32_t const width, uint32_t const height,
            std::string_view const title, WindowType const type)
        :
            GLFWDependent (Detail::AssertInPlaceTag {}),
            GLFWWindow (width, height, title, type)
        {
            WindowInput::register_callbacks (GLFWWindow::myHandle);
        }

    private:
        [[nodiscard]] vk::raii::SurfaceKHR
        create_surface (vk::raii::Instance const& instance) {
            return WindowSurface::create_surface (instance, GLFWWindow::myHandle);
        }

        friend class Core;
    };
}

#endif