#ifndef IGNIS_DETAIL_SYNC_MOCKS_HXX
#define IGNIS_DETAIL_SYNC_MOCKS_HXX

#include <type_traits>

namespace Ignis::Detail
{
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