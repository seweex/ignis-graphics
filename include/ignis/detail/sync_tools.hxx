#ifndef IGNIS_GRAPHICS_SYNC_TOOLS_HXX
#define IGNIS_GRAPHICS_SYNC_TOOLS_HXX

#include <boost/container/small_vector.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/hints.hxx>

namespace Ignis::Detail
{
    class SyncTools final :
        public DeviceDependent
    {
        [[nodiscard]] boost::container::small_vector <vk::raii::Semaphore, Hints::images_count>
        make_semaphores (uint32_t const count) const
        {
            boost::container::small_vector <vk::raii::Semaphore, Hints::images_count> semaphores;
            semaphores.reserve(count);

            vk::SemaphoreCreateInfo constexpr createInfo;

            for (uint32_t i = 0; i < count; ++i)
                semaphores.emplace_back (DeviceDependent::get_device(), createInfo);

            return semaphores;
        }

        [[nodiscard]] boost::container::small_vector <vk::raii::Fence, Hints::images_count>
        make_fences (uint32_t const count, bool const signaled) const
        {
            boost::container::small_vector <vk::raii::Fence, Hints::images_count> fences;
            fences.reserve(count);

            auto const& device = DeviceDependent::get_device();

            vk::FenceCreateInfo const createInfo
                { signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlags{} };

            for (uint32_t i = 0; i < count; ++i)
                fences.emplace_back (device, createInfo);

            return fences;
        }

    public:
        SyncTools (std::weak_ptr <Graphics::Core> const& core, uint32_t const frames)
        :
            CoreDependent (core),

            myImageAvailableSemaphores (make_semaphores (frames)),
            myRenderCompletedSemaphores (make_semaphores (frames)),
            myTransferCompletedSemaphores (make_semaphores (frames)),

            myInFlightFences (make_fences (frames, true)),
            myTransferFences (make_fences (frames, true))
        {}

        SyncTools (SyncTools &&) noexcept = default;
        SyncTools (SyncTools const&) = delete;

        SyncTools& operator=(SyncTools &&) noexcept = default;
        SyncTools& operator=(SyncTools const&) = delete;

        [[nodiscard]] vk::Semaphore
        get_image_available_semaphore (uint32_t const frame) {
            assert (is_valid());
            return myImageAvailableSemaphores.at(frame);
        }

        [[nodiscard]] vk::Semaphore
        get_render_completed_semaphore (uint32_t const frame) {
            assert (is_valid());
            return myRenderCompletedSemaphores.at(frame);
        }

        [[nodiscard]] vk::Semaphore
        get_transfer_completed_semaphore (uint32_t const frame) {
            assert (is_valid());
            return myTransferCompletedSemaphores.at(frame);
        }

        [[nodiscard]] vk::Fence
        get_inflight_fence (uint32_t const frame) {
            assert (is_valid());
            return myInFlightFences.at(frame);
        }

        [[nodiscard]] vk::Fence
        get_transfer_fence (uint32_t const frame) {
            assert (is_valid());
            return myTransferFences.at(frame);
        }

        [[nodiscard]] bool
        is_valid () const noexcept
        {
            return
                !myImageAvailableSemaphores.empty() &&
                !myRenderCompletedSemaphores.empty() &&
                !myTransferCompletedSemaphores.empty() &&
                !myInFlightFences.empty() &&
                !myTransferFences.empty();
        }

    private:
        boost::container::small_vector <vk::raii::Semaphore, Hints::images_count>
        myImageAvailableSemaphores;

        boost::container::small_vector <vk::raii::Semaphore, Hints::images_count>
        myRenderCompletedSemaphores;

        boost::container::small_vector <vk::raii::Semaphore, Hints::images_count>
        myTransferCompletedSemaphores;

        boost::container::small_vector <vk::raii::Fence, Hints::images_count>
        myInFlightFences;

        boost::container::small_vector <vk::raii::Fence, Hints::images_count>
        myTransferFences;
    };
}

#endif