#ifndef IGNIS_DETAIL_DEBUG_ASSERT_HXX
#define IGNIS_DETAIL_DEBUG_ASSERT_HXX

#include <thread>
#include <concepts>
#include <exception>
#include <string_view>

namespace Ignis::Utility
{
#if NDEBUG
    class CreationThreadAsserter
    {
    protected:
        void assert_creation_thread () const noexcept {}
    };
#else
    class CreationThreadAsserter
    {
    protected:
        CreationThreadAsserter () noexcept :
            myCreationThreadID (std::this_thread::get_id())
        {}

        void assert_creation_thread () const
        {
            thread_local auto const this_thread_id = std::this_thread::get_id();

            if (myCreationThreadID != this_thread_id)
                [[unlikely]] std::abort();
        }

    private:
        std::thread::id myCreationThreadID;
    };
#endif

    template <std::derived_from <std::exception> Exception>
#if NDEBUG
    void debug_throw (std::string_view) noexcept {}
#else
    [[noreturn]] void debug_throw (std::string_view const description)
    {
        throw Exception { description.data() };
    }
#endif
}

#endif