#ifndef IGNIS_DETAIL_GLFW_CONTEXT_HXX
#define IGNIS_DETAIL_GLFW_CONTEXT_HXX

#include <cassert>
#include <stdexcept>

#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

#include <ignis/detail/debug_assert.hxx>
#include <ignis/detail/include_glfw.hxx>

namespace Ignis::Detail
{
    class GLFWSolitudeLock
    {
    protected:
        GLFWSolitudeLock() noexcept
        {
            bool exists = false;
            bool const registered = does_exist.compare_exchange_strong (exists, true,
                std::memory_order_acq_rel, std::memory_order_acquire);

            assert (!exists && registered);
        }

        ~GLFWSolitudeLock() noexcept {
            does_exist.store (false, std::memory_order_release);
        }

    public:
        GLFWSolitudeLock (GLFWSolitudeLock &&) = delete;
        GLFWSolitudeLock (GLFWSolitudeLock const&) = delete;

        GLFWSolitudeLock& operator=(GLFWSolitudeLock &&) = delete;
        GLFWSolitudeLock& operator=(GLFWSolitudeLock const&) = delete;

    private:
        static inline std::atomic_bool does_exist = false;
    };

    class GLFWContext final :
        private GLFWSolitudeLock,
        public CreationThreadAsserter
    {
    public:
        GLFWContext()
        {
            if (glfwInit() != GLFW_TRUE) [[unlikely]]
                throw std::runtime_error("Failed to initialize GLFW");

            glfwWindowHint (GLFW_CLIENT_API, GLFW_NO_API);
        }

        ~GLFWContext() noexcept {
            glfwTerminate();
        }

        GLFWContext (GLFWContext &&) = delete;
        GLFWContext (GLFWContext const&) = delete;

        GLFWContext& operator=(GLFWContext &&) = delete;
        GLFWContext& operator=(GLFWContext const&) = delete;

        [[nodiscard]] boost::container::small_vector <const char*, 5>
        get_extensions() const
        {
            uint32_t count;
            auto const requiredExtensions = glfwGetRequiredInstanceExtensions(&count);

            boost::container::small_vector <const char*, 5> extensions;
#if NDEBUG
            extensions.reserve(count);
#else
            extensions.reserve (count + 1);
            extensions.emplace_back (VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

            for (uint32_t i = 0; i < count; ++i)
                extensions.emplace_back (requiredExtensions[i]);

            return extensions;
        }

        [[nodiscard]] boost::container::static_vector <const char*, 1>
        get_layers() const noexcept {
#if NDEBUG
            return {};
#else
            return { "VK_LAYER_KHRONOS_validation" };
#endif
        }
    };
}

#endif