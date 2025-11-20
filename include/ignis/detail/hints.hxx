#ifndef IGNIS_DETAIL_HINTS_HXX
#define IGNIS_DETAIL_HINTS_HXX

#include <cstdint>
#include <limits>

namespace Ignis::Detail
{
    struct Hints {
        static constexpr uint64_t wait_timeout = std::numeric_limits <uint64_t>::max();
        static constexpr uint32_t images_count = 3;
        static constexpr uint32_t staged_transfers_per_frame = 15;
    };
}

#endif