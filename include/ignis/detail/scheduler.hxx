#ifndef IGNIS_DETAIL_SCHEDULER_HXX
#define IGNIS_DETAIL_SCHEDULER_HXX

#include <boost/container/small_vector.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/enable_sync.hxx>

#include <ignis/detail/hints.hxx>
#include <ignis/detail/sync_tools.hxx>
#include <ignis/detail/double_command_buffer.hxx>

namespace Ignis::Detail
{
    class SchedulerBase :
        public virtual DeviceDependent
    {
    protected:
        SchedulerBase () noexcept = default;

    public:
        SchedulerBase (SchedulerBase &&) noexcept = default;
        SchedulerBase (SchedulerBase const&) = delete;

        SchedulerBase& operator=(SchedulerBase &&) noexcept = default;
        SchedulerBase& operator=(SchedulerBase const&) = delete;

    protected:
        [[nodiscard]] vk::raii::CommandPool
        make_pool (uint32_t const family) const
        {
            auto const& device = DeviceDependent::get_device();

            vk::CommandPoolCreateInfo const createInfo
                { vk::CommandPoolCreateFlagBits::eResetCommandBuffer, family };

            return { device, createInfo };
        }

        [[nodiscard]] vk::raii::CommandBuffer
        make_command_buffer (vk::CommandPool const pool) const
        {
            auto const& device = DeviceDependent::get_device();

            vk::CommandBufferAllocateInfo const allocateInfo
                { pool, vk::CommandBufferLevel::ePrimary, 1 };

            vk::raii::CommandBuffers buffers { device, allocateInfo };
            return std::move (buffers.front());
        }

    public:
        void wait_fence (vk::Fence const fence) const
        {
            auto& device = DeviceDependent::get_device();

            if (auto const result = device.waitForFences({ 1, &fence }, true, Hints::wait_timeout);
                result != vk::Result::eSuccess) [[unlikely]]
                throw std::runtime_error ("Failed to wait for fence");

            device.resetFences({ 1, &fence });
        }
    };

    template <bool InternalSync>
    class GraphicsScheduler :
        public virtual CreationThreadAsserter,
        public virtual SchedulerBase
    {
        [[nodiscard]] boost::container::small_vector <vk::raii::CommandBuffer, Hints::images_count>
        make_frames_command_buffers (uint32_t const frames) const
        {
            boost::container::small_vector <vk::raii::CommandBuffer, Hints::images_count> buffers;
            buffers.reserve (frames);

            for (uint32_t i = 0; i < frames; ++i)
                buffers.emplace_back (SchedulerBase::make_command_buffer (*myPool));

            return buffers;
        }

    public:
        GraphicsScheduler (std::weak_ptr<Graphics::Core> const& core, uint32_t const frames)
        :
            CoreDependent (core),

            myQueue (DeviceDependent::get_device().getQueue(
                DeviceDependent::get_indices().families.graphics,
                DeviceDependent::get_indices().queues.graphics)),

            myPool (make_pool (DeviceDependent::get_indices().families.graphics)),
            myBuffers (make_frames_command_buffers (frames))
        {}

        GraphicsScheduler (GraphicsScheduler&&) = delete;
        GraphicsScheduler (GraphicsScheduler const&) = delete;

        GraphicsScheduler& operator=(GraphicsScheduler&&) = delete;
        GraphicsScheduler& operator=(GraphicsScheduler const&) = delete;

        [[nodiscard]] std::pair <vk::raii::CommandBuffer&,
            std::conditional_t <InternalSync, std::unique_lock <std::mutex>, LockMock>>
        get_graphics_command_buffer (uint32_t const frame)
        {
            auto& buffer = myBuffers.at (frame);
            auto lock = lock_mutex <InternalSync, std::unique_lock> (myBuffersMutex);

            return { buffer, std::move (lock) };
        }

        void execute_graphics (uint32_t const frame, SyncTools& syncTools)
        {
            CreationThreadAsserter::assert_creation_thread();
            assert (syncTools.is_valid());

            auto const transferFence = syncTools.get_transfer_fence (frame);
            auto const inflightFence = syncTools.get_inflight_fence (frame);

            std::array <vk::PipelineStageFlags, 2>
            constexpr waitStages = {
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eTransfer
            };

            std::array const waitSemaphores = {
                syncTools.get_image_available_semaphore (frame),
                syncTools.get_transfer_completed_semaphore (frame)
            };

            std::array const signalSemaphores = {
                syncTools.get_render_completed_semaphore (frame)
            };

            [[maybe_unused]] auto const lock =
                lock_mutex <InternalSync, std::unique_lock> (myBuffersMutex);

            auto const buffer = *myBuffers.at (frame);

            vk::SubmitInfo const submitInfo {
                static_cast<uint32_t>(waitSemaphores.size()),
                waitSemaphores.data(),
                waitStages.data(),
                1, &buffer,
                static_cast<uint32_t>(signalSemaphores.size()),
                signalSemaphores.data()
            };

            SchedulerBase::wait_fence (transferFence);
            myQueue.submit ({ 1, &submitInfo }, inflightFence);
        }

    private:
        vk::raii::Queue myQueue;
        vk::raii::CommandPool myPool;

        [[no_unique_address]] EnableMutex <InternalSync, std::mutex> myBuffersMutex;

        boost::container::small_vector
            <vk::raii::CommandBuffer, Hints::images_count>
        myBuffers;
    };

    template <bool InternalSync>
    class TransferScheduler :
        public virtual CreationThreadAsserter,
        public virtual SchedulerBase
    {
        explicit TransferScheduler (
            std::weak_ptr<Graphics::Core> const& core,
            [[maybe_unused]] std::true_type const initializeMultithread)
        requires (InternalSync) :
            CoreDependent (core),

            myQueue (DeviceDependent::get_device().getQueue(
                DeviceDependent::get_indices().families.transfer,
                DeviceDependent::get_indices().queues.transfer)),

            myPool (SchedulerBase::make_pool (DeviceDependent::get_indices().families.transfer)),

            myCommandBuffers (
                SchedulerBase::make_command_buffer (*myPool),
                SchedulerBase::make_command_buffer (*myPool))
        {}

        explicit TransferScheduler (
            std::weak_ptr<Graphics::Core> const& core,
            [[maybe_unused]] std::false_type const initializeMultithread)
        requires (!InternalSync) :
            CoreDependent (core),

            myQueue (DeviceDependent::get_device().getQueue(
                DeviceDependent::get_indices().families.transfer,
                DeviceDependent::get_indices().queues.transfer)),

            myPool (SchedulerBase::make_pool (DeviceDependent::get_indices().families.transfer)),
            myCommandBuffers (SchedulerBase::make_command_buffer (*myPool))
        {}

    public:
        explicit TransferScheduler (std::weak_ptr<Graphics::Core> const& core) :
            TransferScheduler (core, std::bool_constant <InternalSync>{})
        {}

        TransferScheduler (TransferScheduler &&) = delete;
        TransferScheduler (TransferScheduler const&) = delete;

        TransferScheduler& operator=(TransferScheduler &&) = delete;
        TransferScheduler& operator=(TransferScheduler const&) = delete;

        [[nodiscard]] auto get_transfer_command_buffer () {
            return myCommandBuffers.get_for_writing ();
        }

        void execute_transfer (uint32_t const frame, SyncTools& syncTools)
        {
            CreationThreadAsserter::assert_creation_thread();
            assert (syncTools.is_valid());

            std::array <vk::PipelineStageFlags, 1>
            constexpr waitStages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

            std::array const waitSemaphores = { syncTools.get_render_completed_semaphore (frame) };
            std::array const signalSemaphores = { syncTools.get_transfer_completed_semaphore (frame) };

            auto const fence = syncTools.get_transfer_fence (frame);
            auto const [buffer, lock] = myCommandBuffers.get_for_execution ();

            vk::SubmitInfo const submitInfo {
                static_cast<uint32_t>(waitSemaphores.size()),
                waitSemaphores.data(),
                waitStages.data(),
                1, &buffer,
                static_cast<uint32_t>(signalSemaphores.size()),
                signalSemaphores.data()
            };

            myQueue.submit ({ 1, &submitInfo }, fence);
        }

    private:
        vk::raii::Queue myQueue;
        vk::raii::CommandPool myPool;

        DoubleCommandBuffer <InternalSync> myCommandBuffers;
    };

    class PresentScheduler :
        public virtual CreationThreadAsserter,
        public virtual SchedulerBase
    {
    public:
        explicit PresentScheduler (std::weak_ptr<Graphics::Core> const& core) :
            CoreDependent (core),
            myQueue (DeviceDependent::get_device().getQueue(
                DeviceDependent::get_indices().families.present,
                DeviceDependent::get_indices().queues.present))
        {}

        PresentScheduler (PresentScheduler &&) = delete;
        PresentScheduler (PresentScheduler const&) = delete;

        PresentScheduler& operator=(PresentScheduler &&) = delete;
        PresentScheduler& operator=(PresentScheduler const&) = delete;

        void present_image (
            vk::SwapchainKHR const swapchain,
            uint32_t const image,
            uint32_t const frame,
            SyncTools& syncTools)
        {
            CreationThreadAsserter::assert_creation_thread();
            assert (syncTools.is_valid());

            std::array const waitSemaphores =
                { syncTools.get_render_completed_semaphore (frame) };

            vk::PresentInfoKHR const presentInfo {
                static_cast <uint32_t> (waitSemaphores.size()),
                waitSemaphores.data(),
                1, &swapchain,
                &image,
                nullptr
            };

            if (auto const result = myQueue.presentKHR (presentInfo);
                result != vk::Result::eSuccess) [[unlikely]]
                throw std::runtime_error ("Failed to present an image");
        }

    private:
        vk::raii::Queue myQueue;
    };
}

#endif