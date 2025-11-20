#ifndef IGNIS_DETAIL_ENABLE_SYNC_HXX
#define IGNIS_DETAIL_ENABLE_SYNC_HXX

#include <type_traits>

namespace Ignis::Detail
{
    template <bool Enable, size_t DefaultAlignment = alignof(void*)>
    inline constexpr size_t SyncAlignment = Enable ? 64 : DefaultAlignment;

    class LockMock {};
    class MutexMock {};

    template <bool Enable, class Mutex>
    using EnableMutex = std::conditional_t <Enable, Mutex, MutexMock>;

    template <bool Enable, template <class> class Lock, class Mutex>
    [[nodiscard]] auto lock_mutex (Mutex& mutex) noexcept (!Enable)
    {
        if constexpr (Enable)
            return Lock <Mutex> { mutex };
        else
            return LockMock {};
    }
}

#endif