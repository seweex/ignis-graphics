#ifndef IGNIS_DETAIL_MEMORY_ALLOCATOR_HXX
#define IGNIS_DETAIL_MEMORY_ALLOCATOR_HXX

#include <vulkan/vulkan_raii.hpp>
#include <ignis/detail/include_vulkan_allocator.hxx>

#include <ignis/detail/core_dependent.hxx>

namespace Ignis::Graphics
{
    enum class MemoryAccess
    {
        transfer,
        to_temporary_mapped,
        to_constantly_mapped,

#if !NDEBUG
        first_enum_value = transfer,
        last_enum_value = to_constantly_mapped
#endif
    };

    enum class MemoryPlacement
    {
        in_device,
        in_host,
        no_matter,

#if !NDEBUG
        first_enum_value = in_device,
        last_enum_value = no_matter
#endif
    };
}

namespace Ignis::Detail
{
    class ResourceMemoryAllocator :
        public virtual CoreDependent
    {
        [[nodiscard]] vma::UniqueAllocator
        make_allocator () const
        {
            auto const& deviceDispatcher = CoreDependent::get_device_dispatcher();
            auto const& instanceDispatcher = CoreDependent::get_instance_dispatcher();
            auto const& contextDispatcher = CoreDependent::get_context_dispatcher();

            vma::VulkanFunctions const vulkanFunctions
            {
                contextDispatcher.vkGetInstanceProcAddr,
                instanceDispatcher.vkGetDeviceProcAddr,
                instanceDispatcher.vkGetPhysicalDeviceProperties,
                instanceDispatcher.vkGetPhysicalDeviceMemoryProperties,
                deviceDispatcher.vkAllocateMemory,
                deviceDispatcher.vkFreeMemory,
                deviceDispatcher.vkMapMemory,
                deviceDispatcher.vkUnmapMemory,
                deviceDispatcher.vkFlushMappedMemoryRanges,
                deviceDispatcher.vkInvalidateMappedMemoryRanges,
                deviceDispatcher.vkBindBufferMemory,
                deviceDispatcher.vkBindImageMemory,
                deviceDispatcher.vkGetBufferMemoryRequirements,
                deviceDispatcher.vkGetImageMemoryRequirements,
                deviceDispatcher.vkCreateBuffer,
                deviceDispatcher.vkDestroyBuffer,
                deviceDispatcher.vkCreateImage,
                deviceDispatcher.vkDestroyImage,
                deviceDispatcher.vkCmdCopyBuffer,
                deviceDispatcher.vkGetBufferMemoryRequirements2KHR,
                deviceDispatcher.vkGetImageMemoryRequirements2KHR,
                deviceDispatcher.vkBindBufferMemory2KHR,
                deviceDispatcher.vkBindImageMemory2KHR,
                instanceDispatcher.vkGetPhysicalDeviceMemoryProperties2KHR,
                deviceDispatcher.vkGetDeviceBufferMemoryRequirements,
                deviceDispatcher.vkGetDeviceImageMemoryRequirements
            };

            vma::AllocatorCreateInfo const createInfo
            {
                {},
                CoreDependent::get_physical_device(),
                CoreDependent::get_device(),
                {},
                nullptr,
                nullptr,
                nullptr,
                &vulkanFunctions,
                CoreDependent::get_instance(),
                static_cast <uint32_t> (CoreDependent::get_device_dispatcher().getVkHeaderVersion()),
                nullptr
            };

            return vma::createAllocatorUnique (createInfo);
        }

        template <Graphics::MemoryAccess Access>
        [[nodiscard]] static constexpr vma::AllocationCreateFlags make_allocation_flags () noexcept
        {
            constexpr auto common_flags =
                vma::AllocationCreateFlagBits::eStrategyBestFit;

            if constexpr (Access == Graphics::MemoryAccess::to_constantly_mapped)
                return
                    vma::AllocationCreateFlagBits::eMapped |
                    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
                    common_flags;

            else if constexpr (Access == Graphics::MemoryAccess::to_temporary_mapped)
                return
                    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
                    common_flags;

            else
                return common_flags;
        }

        template <Graphics::MemoryPlacement Placement>
        [[nodiscard]] static constexpr vma::MemoryUsage make_usage_flags () noexcept
        {
            if constexpr (Placement == Graphics::MemoryPlacement::in_device)
                return vma::MemoryUsage::eAutoPreferDevice;

            else if constexpr (Placement == Graphics::MemoryPlacement::in_host)
                return vma::MemoryUsage::eAutoPreferHost;

            else
                return vma::MemoryUsage::eAuto;
        }

    protected:
        ResourceMemoryAllocator () :
            myAllocator (make_allocator())
        {}

        template <Graphics::MemoryAccess Access, Graphics::MemoryPlacement Placement>
        [[nodiscard]] vma::UniqueAllocation make_allocation (vk::Buffer const buffer)
        {
            vma::AllocationCreateInfo const allocationInfo {
                make_allocation_flags <Access> (),
                make_usage_flags <Placement> (),
                {},
                {},
                {},
                nullptr,
                nullptr,
                1.f
            };

            return myAllocator->allocateMemoryForBufferUnique (buffer, allocationInfo);
        }

        template <Graphics::MemoryAccess Access, Graphics::MemoryPlacement Placement>
        [[nodiscard]] vma::UniqueAllocation make_allocation (vk::Image const image)
        {
            vma::AllocationCreateInfo const allocationInfo {
                make_allocation_flags <Access> (),
                make_usage_flags <Placement> (),
                {},
                {},
                {},
                nullptr,
                nullptr,
                1.f
            };

            return myAllocator->allocateMemoryForImageUnique (image, allocationInfo);
        }

        vma::UniqueAllocator myAllocator;
    };

    template <bool InternalSync>
    class ResourceMemoryFactory :
        public virtual ResourceMemoryAllocator
    {
    protected:
        ResourceMemoryFactory () = default;

        [[nodo]]

    private:
        [[no_unique_address]] EnableMutex <InternalSync, std::mutex> myMutex;

        alignas (SyncAlignment <InternalSync>)
        boost::unordered::unordered_flat_set
            <vma::UniqueAllocation, VulkanHash, VulkanEquals>
        myAllocations;
    };
}

#endif