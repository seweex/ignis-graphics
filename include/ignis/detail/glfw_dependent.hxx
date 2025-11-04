#ifndef IGNIS_DETAIL_GLFW_DEPENDENT_HXX
#define IGNIS_DETAIL_GLFW_DEPENDENT_HXX

#include <memory>
#include <shared_mutex>

#include <ignis/detail/glfw_context.hxx>

namespace Ignis::Detail
{
    class GLFWDispatcher final
    {
    public:
        [[nodiscard]] std::shared_ptr <GLFWContext>
        acquire_context ()
        {
            std::unique_lock lock { myMutex };

            if (auto context = myContext.lock()) [[likely]]
                return context;

            else {
                context = std::make_shared <GLFWContext> ();
                myContext = context;

                return context;
            }
        }

        void assert_context_creation_thread() const
        {
            std::shared_lock lock { myMutex };

            auto const context = myContext.lock();
            assert (context);

            context->assert_creation_thread();
        }

    private:
        std::shared_mutex mutable myMutex;
        std::weak_ptr <GLFWContext> myContext;
    };

    class GLFWDependent
    {
    protected:
        GLFWDependent () :
            myContextKeeper (dispatcher.acquire_context())
        {}

        explicit GLFWDependent (AssertInPlaceTag) :
            myContextKeeper (dispatcher.acquire_context())
        {
            myContextKeeper->assert_creation_thread();
        }

        [[nodiscard]] auto get_extensions() const noexcept {
            return myContextKeeper->get_extensions();
        }

        [[nodiscard]] auto get_layers() const noexcept {
            return myContextKeeper->get_layers();
        }

        static void assert_context_creation_thread() {
            dispatcher.assert_context_creation_thread();
        }

    private:
        static inline GLFWDispatcher dispatcher;

        std::shared_ptr <GLFWContext const> myContextKeeper;
    };
}

#endif