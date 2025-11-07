#ifndef IGNIS_DETAIL_SCHEDULER_HXX
#define IGNIS_DETAIL_SCHEDULER_HXX

#include <boost/container/small_vector.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/sync_mocks.hxx>

namespace Ignis::Detail
{
    template <bool InternalSync>
    class PendingExecutionBuffers;

    template <bool InternalSync>
    class CommandPools;

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

        struct alignas(64) {
            std::vector <vk::raii::CommandBuffer> myPendingBuffers;
            std::vector <vk::CommandBuffer> myPendingHandles;
        };

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

    template <>
    class CommandPools <true>
    {
        static thread_local auto const inline this_thread_id = std::this_thread::get_id();

        [[nodiscard]] std::optional <vk::CommandPool>
        find_pool () const noexcept
        {
            std::shared_lock lock { myMutex };

            if (auto const iter = myPools.find (this_thread_id);
                iter != myPools.end()) [[likely]]
                return iter->second;
            else
                return std::nullopt;
        }

        [[nodiscard]] vk::CommandPool
        emplace_pool ()
        {
            vk::CommandPoolCreateInfo const createInfo
                { vk::CommandPoolCreateFlagBits::eTransient, myFamily };

            vk::raii::CommandPool pool { myDevice, createInfo };

            std::unique_lock lock { myMutex };
            return myPools.emplace (this_thread_id, std::move (pool)).first->second;
        }

    public:
        CommandPools (vk::raii::Device& device, uint32_t const family)
        noexcept :
            myDevice (device),
            myFamily (family)
        {}

        CommandPools (CommandPools &&) = delete;
        CommandPools (CommandPools const&) = delete;

        CommandPools& operator=(CommandPools &&) = delete;
        CommandPools& operator=(CommandPools const&) = delete;

        [[nodiscard]] vk::CommandPool
        acquire_pool ()
        {
            if (auto const pool = find_pool();
                pool.has_value()) [[likely]]
                return *pool;
            else
                return emplace_pool ();
        }

    private:
        vk::raii::Device& myDevice;
        uint32_t myFamily;

        std::shared_mutex mutable myMutex;

        alignas (64) boost::unordered::unordered_flat_map
            <std::thread::id, vk::raii::CommandPool, std::hash <std::thread::id>>
        myPools;
    };

    template <>
    class CommandPools <false>
    {
        [[nodiscard]] static vk::raii::CommandPool
        make_command_pool (vk::raii::Device& device, uint32_t const family)
        {
            vk::CommandPoolCreateInfo const createInfo
                { vk::CommandPoolCreateFlagBits::eTransient, family };

            return { device, createInfo };
        }

    public:
        CommandPools (vk::raii::Device& device, uint32_t const family) :
            myPool (make_command_pool (device, family))
        {}

        [[nodiscard]] vk::CommandPool
        acquire_pool () noexcept {
           return myPool;
        }

    private:
        vk::raii::CommandPool myPool;
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

    public:
        SchedulerBase () noexcept {
            std::terminate();
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

        [[nodiscard]] vk::raii::CommandBuffer
        make_command_buffer (vk::CommandPool const pool) const
        {
            auto const& device = CoreDependent::get_device();

            vk::CommandBufferAllocateInfo const allocateInfo
                { pool, vk::CommandBufferLevel::ePrimary, 1 };

            vk::raii::CommandBuffers buffers { device, allocateInfo };
            return std::move (buffers.front());
        }

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

    private:
        alignas (64) boost::container::small_vector <vk::raii::Semaphore, images_hint>
        myImageAvailableSemaphores;

        alignas (64) boost::container::small_vector <vk::raii::Semaphore, images_hint>
        myRenderCompletedSemaphores;

        alignas (64) boost::container::small_vector <vk::raii::Semaphore, images_hint>
        myTransferCompletedSemaphores;

        alignas (64) boost::container::small_vector <vk::raii::Fence, images_hint>
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
            SchedulerBase (frames),

            myQueue (CoreDependent::get_device().getQueue(
                CoreDependent::get_indices().families.graphics,
                CoreDependent::get_indices().queues.graphics)),

            myPools (CoreDependent::get_device(), CoreDependent::get_indices().families.graphics),
            myFrameCommands (frames)
        {}

        [[nodiscard]] vk::raii::CommandBuffer
        make_graphics_command_buffers () {
            auto const pool = myPools.acquire_pool();
            return SchedulerBase::make_command_buffer (pool);
        }

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

        CommandPools <InternalSync> myPools;

        boost::container::small_vector <PendingExecutionBuffers <InternalSync>, SchedulerBase::images_hint>
        myFrameCommands;
    };

    template <bool InternalSync>
    class TransferScheduler :
        public virtual SchedulerBase
    {
    protected:
        TransferScheduler ()
        :
            myQueue (CoreDependent::get_device().getQueue(
                CoreDependent::get_indices().families.transfer,
                CoreDependent::get_indices().queues.transfer)),

            myPools (CoreDependent::get_device(), CoreDependent::get_indices().families.transfer)
        {}

        [[nodiscard]] vk::raii::CommandBuffer
        make_transfer_command_buffers () {
            auto const pool = myPools.acquire_pool();
            return SchedulerBase::make_command_buffer (pool);
        }

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

        CommandPools <InternalSync> myPools;
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
            GraphicsScheduler (frames)
        {}
    };
}

#endif