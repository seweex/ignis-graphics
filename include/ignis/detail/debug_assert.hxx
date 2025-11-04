#ifndef IGNIS_DETAIL_DEBUG_ASSERT_HXX
#define IGNIS_DETAIL_DEBUG_ASSERT_HXX

#include <thread>
#include <concepts>
#include <exception>
#include <string_view>

namespace Ignis::Detail
{
#if NDEBUG
    class CreationThreadAsserter
    {
    public:
        void assert_creation_thread () const noexcept {}
    };
#else
    class CreationThreadAsserter
    {
    protected:
        CreationThreadAsserter () noexcept :
            myCreationThreadID (std::this_thread::get_id())
        {}

    public:
        void assert_creation_thread () const noexcept
        {
            thread_local auto const this_thread_id = std::this_thread::get_id();

            assert (myCreationThreadID != this_thread_id);
        }

    private:
        std::thread::id myCreationThreadID;
    };
#endif

    struct AssertInPlaceTag {};

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