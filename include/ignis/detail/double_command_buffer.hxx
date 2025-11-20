#ifndef IGNIS_DETAIL_DOUBLE_COMMAND_BUFFER_HXX
#define IGNIS_DETAIL_DOUBLE_COMMAND_BUFFER_HXX

#include <mutex>
#include <utility>

#include <vulkan/vulkan_raii.hpp>
#include <ignis/detail/enable_sync.hxx>

namespace Ignis::Detail
{
    template <bool EnableDoubleBuffering>
    class DoubleCommandBuffer;

    template <>
    class DoubleCommandBuffer <true>
    {
    public:
        DoubleCommandBuffer (
            vk::raii::CommandBuffer&& executing,
            vk::raii::CommandBuffer&& collector)
        :
            myCollectorBuffer (std::move (collector)),
            myExecutingBuffer (std::move (executing))
        {}

        DoubleCommandBuffer (DoubleCommandBuffer&&) = delete;
        DoubleCommandBuffer (DoubleCommandBuffer const&) = delete;

        DoubleCommandBuffer& operator=(DoubleCommandBuffer&&) = delete;
        DoubleCommandBuffer& operator=(DoubleCommandBuffer const&) = delete;

        [[nodiscard]] std::pair <vk::raii::CommandBuffer&, std::unique_lock <std::mutex>>
        get_for_writing () {
            return { myCollectorBuffer, std::unique_lock{ myMutex } };
        }

        [[nodiscard]] std::pair <vk::CommandBuffer, std::unique_lock <std::mutex>>
        get_for_execution ()
        {
            myExecutingBuffer.reset ();
            std::unique_lock lock { myMutex };

            myExecutingBuffer.swap (myCollectorBuffer);
            return { myExecutingBuffer, std::move (lock) };
        }

    private:
        std::mutex myMutex;

        vk::raii::CommandBuffer myCollectorBuffer;
        vk::raii::CommandBuffer myExecutingBuffer;
    };

    template <>
    class DoubleCommandBuffer <false>
    {
    public:
        explicit DoubleCommandBuffer (vk::raii::CommandBuffer&& buffer) :
            myBuffer (std::move (buffer))
        {}

        DoubleCommandBuffer (DoubleCommandBuffer&&) = delete;
        DoubleCommandBuffer (DoubleCommandBuffer const&) = delete;

        DoubleCommandBuffer& operator=(DoubleCommandBuffer&&) = delete;
        DoubleCommandBuffer& operator=(DoubleCommandBuffer const&) = delete;

        [[nodiscard]] std::pair <vk::raii::CommandBuffer&, LockMock>
        get_for_writing () noexcept {
            return { myBuffer, LockMock{} };
        }

        [[nodiscard]] std::pair <vk::CommandBuffer, LockMock>
        get_for_execution () noexcept {
            myBuffer.reset ();
            return { myBuffer, LockMock{} };
        }

    private:
        vk::raii::CommandBuffer myBuffer;
    };
}

#endif