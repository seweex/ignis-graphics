#ifndef IGNIS_DETAIL_SCHEDULER_HXX
#define IGNIS_DETAIL_SCHEDULER_HXX

#include <boost/container/small_vector.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/sync_mocks.hxx>

namespace Ignis::Detail
{
    template <bool InternalSync>
    class PendingExecutionBuffers;

    template <>
    class PendingExecutionBuffers <true>
    {
    public:
        void postpone_commands (vk::raii::CommandBuffer&& buffer)
        {
            std::lock_guard lock { myPendingMutex };

            myPendingBuffers.reserve (myPendingBuffers.size() + 1);
            myPendingHandles.reserve (myPendingHandles.size() + 1);

            vk::CommandBuffer const handle = myPendingBuffers.emplace_back (std::move (buffer));
            myPendingHandles.emplace_back (handle);
        }

        void begin_execution ()
        {
            myExecutingHandles.clear();
            myExecutingBuffers.clear();

            {
                std::lock_guard lock { myPendingMutex };

                myExecutingBuffers.swap (myPendingBuffers);
                myExecutingHandles.swap (myPendingHandles);
            }
        }

        [[nodiscard]] std::span <vk::CommandBuffer const>
        get_executing_handles () const noexcept {
            return { myExecutingHandles.data(), myExecutingHandles.size() };
        }

        [[nodiscard]] bool empty() const noexcept {
            return myExecutingBuffers.empty();
        }

    private:
        std::mutex myPendingMutex;

        std::vector <vk::raii::CommandBuffer> myPendingBuffers;
        std::vector <vk::CommandBuffer> myPendingHandles;

        std::vector <vk::raii::CommandBuffer> myExecutingBuffers;
        std::vector <vk::CommandBuffer> myExecutingHandles;
    };

    template <>
    class PendingExecutionBuffers <false>
    {
    public:
        void postpone_commands (vk::raii::CommandBuffer&& buffer)
        {
            myBuffers.reserve (myBuffers.size() + 1);
            myHandles.reserve (myHandles.size() + 1);

            vk::CommandBuffer const handle = myBuffers.emplace_back (std::move (buffer));
            myHandles.emplace_back (handle);
        }

        void begin_execution () noexcept {}

        [[nodiscard]] std::span <vk::CommandBuffer const>
        get_executing_handles () const noexcept {
            return { myHandles.data(), myHandles.size() };
        }

        [[nodiscard]] bool empty() const noexcept {
            return myBuffers.empty();
        }

    private:
        std::vector <vk::raii::CommandBuffer> myBuffers;
        std::vector <vk::CommandBuffer> myHandles;
    };

    class SchedulerBase :
        public virtual CoreDependent
    {
    protected:
        static constexpr size_t images_hint = 3;

    private:
        static constexpr uint64_t wait_timeout = std::numeric_limits <uint64_t>::max();

        [[nodiscard]] boost::container::small_vector <vk::raii::Semaphore, images_hint>
        make_semaphores (uint32_t const count) const
        {
            boost::container::small_vector <vk::raii::Semaphore, images_hint> semaphores;
            semaphores.reserve(count);

            vk::SemaphoreCreateInfo constexpr createInfo;

            for (uint32_t i = 0; i < count; ++i)
                semaphores.emplace_back (CoreDependent::get_device(), createInfo);

            return semaphores;
        }

        [[nodiscard]] boost::container::small_vector <vk::raii::Fence, images_hint>
        make_fences (uint32_t const count) const
        {
            boost::container::small_vector <vk::raii::Fence, images_hint> fences;
            fences.reserve(count);

            vk::FenceCreateInfo constexpr createInfo
                { vk::FenceCreateFlagBits::eSignaled };

            for (uint32_t i = 0; i < count; ++i)
                fences.emplace_back (CoreDependent::get_device(), createInfo);

            return fences;
        }

    protected:
        explicit SchedulerBase (uint32_t const frames)
        :
            myImageAvailableSemaphores    (make_semaphores (frames)),
            myRenderCompletedSemaphores   (make_semaphores (frames)),
            myTransferCompletedSemaphores (make_semaphores (frames)),

            myInFlightFences (make_fences (frames)),
            myGraphicsWaitsTransfer (false)
        {}

        [[nodiscard]] vk::Semaphore
        get_image_available_semaphore (uint32_t const frame) {
            return myImageAvailableSemaphores.at(frame);
        }

        [[nodiscard]] vk::Semaphore
        get_render_completed_semaphore (uint32_t const frame) {
            return myRenderCompletedSemaphores.at(frame);
        }

        [[nodiscard]] vk::Semaphore
        get_transfer_completed_semaphore (uint32_t const frame) {
            return myTransferCompletedSemaphores.at(frame);
        }

        [[nodiscard]] vk::Fence
        get_inflight_fence (uint32_t const frame) {
            return myInFlightFences.at(frame);
        }

        void wait_fence (vk::Fence const fence) const
        {
            auto& device = CoreDependent::get_device();

            if (auto const result = device.waitForFences({ 1, &fence }, true, wait_timeout);
                result != vk::Result::eSuccess) [[unlikely]]
                throw std::runtime_error ("Failed to wait for fence");

            device.resetFences({ 1, &fence });
        }

        void ask_for_waiting_for_transfer () noexcept {
            myGraphicsWaitsTransfer.store (true, std::memory_order_release);
        }

        [[nodiscard]] bool take_transfer_pause_flag () noexcept
        {
            bool shouldWait = true;
            bool const flagTaken = myGraphicsWaitsTransfer.compare_exchange_strong (shouldWait, false,
                std::memory_order_acq_rel, std::memory_order_acquire);

            return shouldWait && flagTaken;
        }

    public:

    private:
        boost::container::small_vector <vk::raii::Semaphore, images_hint>
        myImageAvailableSemaphores;

        boost::container::small_vector <vk::raii::Semaphore, images_hint>
        myRenderCompletedSemaphores;

        boost::container::small_vector <vk::raii::Semaphore, images_hint>
        myTransferCompletedSemaphores;

        boost::container::small_vector <vk::raii::Fence, images_hint>
        myInFlightFences;

        std::atomic_bool myGraphicsWaitsTransfer;
    };

    template <bool InternalSync>
    class GraphicsScheduler :
        public virtual SchedulerBase
    {
    protected:
        explicit GraphicsScheduler (uint32_t const frames)
        :
            myQueue (CoreDependent::get_device().getQueue(
                CoreDependent::get_indices().families.graphics,
                CoreDependent::get_indices().queues.graphics)),

            myFrameCommands (frames)
        {}

    public:
        void postpone_graphics_commands (
            vk::raii::CommandBuffer&& commands, uint32_t const frame)
        {
            auto& buffers = myFrameCommands.at(frame);
            buffers.postpone_commands (std::move (commands));
        }

        void submit_graphics_commands (uint32_t const frame)
        {
            auto& buffers = myFrameCommands.at(frame);
            buffers.begin_execution();

            assert (!buffers.empty());

            auto const commands = buffers.get_executing_handles();
            auto const fence = SchedulerBase::get_inflight_fence (frame);

            std::array const waitSemaphores = {
                SchedulerBase::get_image_available_semaphore (frame),
                SchedulerBase::get_transfer_completed_semaphore (frame)
            };

            std::array constexpr waitStages = {
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eTransfer
            };

            std::array const signalSemaphores =
                { SchedulerBase::get_render_completed_semaphore (frame) };

            vk::SubmitInfo const submitInfo
            {
                static_cast <uint32_t> (waitSemaphores.size()),
                waitSemaphores.data(),
                waitStages.data(),
                static_cast <uint32_t> (commands.size()),
                commands.data(),
                static_cast <uint32_t> (signalSemaphores.size()),
                signalSemaphores.data()
            };

            myQueue.submit ({ 1, &submitInfo }, fence);
        }

    private:
        vk::raii::Queue myQueue;

        boost::container::small_vector
            <PendingExecutionBuffers <InternalSync>, SchedulerBase::images_hint>
        myFrameCommands;
    };

    template <bool InternalSync>
    class TransferScheduler :
        public virtual SchedulerBase
    {
    protected:
        TransferScheduler () :
            myQueue (CoreDependent::get_device().getQueue(
                CoreDependent::get_indices().families.transfer,
                CoreDependent::get_indices().queues.transfer))
        {}

    public:
        void postpone_transfer_commands (vk::raii::CommandBuffer&& commands) {
            myCommands.postpone_commands (std::move (commands));
        }

        void submit_transfer_commands (uint32_t const frame)
        {
            myCommands.begin_execution();
            assert (!myCommands.empty());

            auto const commands = myCommands.get_executing_handles();

            std::array const signalSemaphores =
                { SchedulerBase::get_transfer_completed_semaphore (frame) };

            vk::SubmitInfo const submitInfo
            {
                0, nullptr,
                nullptr,
                static_cast <uint32_t> (commands.size()),
                commands.data(),
                static_cast <uint32_t> (signalSemaphores.size()),
                signalSemaphores.data()
            };

            myQueue.submit ({ 1, &submitInfo });
        }

    private:
        vk::raii::Queue myQueue;
        PendingExecutionBuffers <InternalSync> myCommands;
    };

    class PresentScheduler :
        public virtual SchedulerBase
    {
    protected:
        PresentScheduler () :
            myQueue (CoreDependent::get_device().getQueue(
                CoreDependent::get_indices().families.present,
                CoreDependent::get_indices().queues.present))
        {}

    public:
        void present_image (vk::SwapchainKHR const swapchain,
            uint32_t const image, uint32_t const frame)
        {
            std::array const waitSemaphores =
                { SchedulerBase::get_render_completed_semaphore (frame) };

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

    template <bool InternalSync>
    class Scheduler :
        public virtual CoreDependent,
        public virtual GraphicsScheduler <InternalSync>,
        public virtual TransferScheduler <InternalSync>,
        public virtual PresentScheduler
    {
        using GraphicsScheduler = GraphicsScheduler <InternalSync>;
        using TransferScheduler = TransferScheduler <InternalSync>;

    protected:
        explicit Scheduler (uint32_t const frames) :
            GraphicsScheduler (frames),
            TransferScheduler (),
            PresentScheduler ()
        {}
    };
}

#endif