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

            assert (myCreationThreadID == this_thread_id);
        }

    private:
        std::thread::id myCreationThreadID;
    };
#endif

    struct AssertInPlaceTag {};

    template <std::derived_from <std::exception> Exception>
    [[noreturn]] void debug_throw (
        [[maybe_unused]] std::string_view const description)
#if NDEBUG
    noexcept {}
#else
    { throw Exception { description.data() }; }
#endif

    template <class Enum>
    constexpr bool is_enum_valid ([[maybe_unused]] Enum const value) noexcept
    {
#if NDEBUG
        return true;
#else
        using Underlying = std::underlying_type_t <Enum>;

        auto constexpr first = static_cast <Underlying> (Enum::first_enum_value);
        auto constexpr last = static_cast <Underlying> (Enum::last_enum_value);

        auto const numericValue = static_cast <Underlying> (value);

        return numericValue >= first && numericValue <= last;
#endif
    }
}

#endif