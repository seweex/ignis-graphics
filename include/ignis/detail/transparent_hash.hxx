#ifndef IGNIS_DETAIL_TRANSPARENT_HASH_HXX
#define IGNIS_DETAIL_TRANSPARENT_HASH_HXX

#include <type_traits>
#include <concepts>

namespace Ignis::Detail
{
    template <class Ty>
    concept Hashable = requires (Ty const& value) {
        { std::hash <Ty> {} (value) } -> std::same_as <size_t>;
    };

    class TransparentHash final
    {
    public:
        using is_transparent = std::true_type;

        template <Hashable Ty>
        [[nodiscard]] size_t operator() (Ty const& value) const noexcept {
            return std::hash <Ty> {} (value);
        }
    };
}

#endif