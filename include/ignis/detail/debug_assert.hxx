#ifndef IGNIS_DETAIL_DEBUG_ASSERT_HXX
#define IGNIS_DETAIL_DEBUG_ASSERT_HXX

#include <thread>
#include <concepts>
#include <exception>
#include <string_view>

namespace Ignis::Detail
{
    struct AssertInPlaceTag {};

#if NDEBUG
    class CreationThreadAsserter
    {
    protected:
        CreationThreadAsserter () noexcept :
            myCreationThreadID (std::this_thread::get_id())
        {}

    public:
        CreationThreadAsserter (CreationThreadAsserter&&) noexcept :
            CreationThreadAsserter ()
        {}

        CreationThreadAsserter (CreationThreadAsserter const&) noexcept :
            CreationThreadAsserter ()
        {}

        CreationThreadAsserter& operator=(CreationThreadAsserter&&) noexcept
        { return *this; }

        CreationThreadAsserter& operator=(CreationThreadAsserter const&) noexcept :
        { return *this; }

        void assert_creation_thread () const noexcept {
            thread_local auto const this_thread_id = std::this_thread::get_id();
            assert (myCreationThreadID == this_thread_id);
        }

    private:
        std::thread::id myCreationThreadID;
    };

    template <std::derived_from <std::exception> Exception>
    void debug_throw (std::string_view) noexcept {}

    template <class Enum>
    constexpr bool is_enum_valid (Enum) noexcept
    { return true; }

#else
    class CreationThreadAsserter
    {
    protected:
        CreationThreadAsserter () noexcept = default;

    public:
        CreationThreadAsserter (CreationThreadAsserter&&) noexcept = default;
        CreationThreadAsserter (CreationThreadAsserter const&) noexcept = default;

        CreationThreadAsserter& operator=(CreationThreadAsserter&&) noexcept = default;
        CreationThreadAsserter& operator=(CreationThreadAsserter const&) noexcept = default;

        void assert_creation_thread () const noexcept {}
    };

    template <std::derived_from <std::exception> Exception>
    [[noreturn]] void debug_throw (std::string_view const description)
    {
        throw Exception { description.data() };
    }

    template <class Enum>
    constexpr bool is_enum_valid (Enum const value) noexcept
    {
        using Underlying = std::underlying_type_t <Enum>;

        auto constexpr first = static_cast <Underlying> (Enum::first_enum_value);
        auto constexpr last = static_cast <Underlying> (Enum::last_enum_value);

        auto const numericValue = static_cast <Underlying> (value);

        return numericValue >= first && numericValue <= last;
    }
#endif
}

#endif