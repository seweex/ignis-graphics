#ifndef IGNIS_DETAIL_RESOURCE_MEMORY_HXX
#define IGNIS_DETAIL_RESOURCE_MEMORY_HXX

#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/include_vulkan_allocator.hxx>
#include <ignis/detail/core_dependent.hxx>

namespace Ignis::Graphics
{
    enum class MemoryAccess
    {
        transfer,
        temporary_mapped,
        constantly_mapped,

#if !NDEBUG
        first_enum_value = transfer,
        last_enum_value = constantly_mapped
#endif
    };

    enum class MemoryPlacement
    {
        device,
        host,
        no_matter,

#if !NDEBUG
        first_enum_value = device,
        last_enum_value = no_matter
#endif
    };
}

namespace Ignis::Detail
{
    class MemoryMapping final
    {
    public:
        MemoryMapping (
            vma::Allocator const allocator,
            vma::Allocation const allocation)
        :
            myAllocator  (allocator),
            myAllocation (allocation),
            myPointer    (allocator.mapMemory (allocation))
        {}

        explicit MemoryMapping (void* const mapped) noexcept :
            myAllocator (nullptr),
            myAllocation (nullptr),
            myPointer (mapped)
        {}

        ~MemoryMapping() noexcept {
            release();
        }

        MemoryMapping (MemoryMapping&& other)
        noexcept :
            myAllocator  (std::exchange (other.myAllocator, nullptr)),
            myAllocation (std::exchange (other.myAllocation, nullptr)),
            myPointer    (std::exchange (other.myPointer, nullptr))
        {}

        MemoryMapping& operator=(MemoryMapping&& other) noexcept
        {
            if (this == &other) [[unlikely]]
                return *this;

            release();

            myAllocator  = std::exchange (other.myAllocator, nullptr);
            myAllocation = std::exchange (other.myAllocation, nullptr);
            myPointer    = std::exchange (other.myPointer, nullptr);

            return *this;
        }

        MemoryMapping (MemoryMapping const&) = delete;
        MemoryMapping& operator=(MemoryMapping const&) = delete;

        [[nodiscard]] bool owns_mapping () const noexcept {
            return myPointer && myAllocator && myAllocation;
        }

        [[nodiscard]] void* get_pointer () const noexcept {
            assert (owns_mapping());
            return myPointer;
        }

        void release () noexcept
        {
            if (owns_mapping()) {
                myAllocator.unmapMemory (myAllocation);
                myPointer = nullptr;
            }
        }

    private:
        vma::Allocator myAllocator;
        vma::Allocation myAllocation;

        void* myPointer;
    };

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
                CoreDependent::get_vulkan_version(),
                nullptr
            };

            return vma::createAllocatorUnique (createInfo);
        }

        template <Graphics::MemoryAccess Access>
        [[nodiscard]] static constexpr vma::AllocationCreateFlags make_allocation_flags () noexcept
        {
            constexpr auto common_flags =
                vma::AllocationCreateFlagBits::eStrategyBestFit;

            if constexpr (Access == Graphics::MemoryAccess::constantly_mapped)
                return
                    vma::AllocationCreateFlagBits::eMapped |
                    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
                    common_flags;

            else if constexpr (Access == Graphics::MemoryAccess::temporary_mapped)
                return
                    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
                    common_flags;

            else
                return common_flags;
        }

    protected:
        ResourceMemoryAllocator () :
            myAllocator (make_allocator())
        {}

        template <Graphics::MemoryAccess Access,
                  Graphics::MemoryPlacement Placement,
                  class AllocateForHandle>
        requires (std::same_as <AllocateForHandle, vk::Buffer> ||
                  std::same_as <AllocateForHandle, vk::Image>)
        [[nodiscard]] vma::UniqueAllocation allocate (AllocateForHandle const resourceHandle)
        {
            vma::AllocationCreateInfo const allocationInfo {
                make_allocation_flags <Access> (),
                {},
                {},
                {},
                {},
                nullptr,
                nullptr,
                1.f
            };

            if constexpr (std::same_as <AllocateForHandle, vk::Buffer>)
                return myAllocator->allocateMemoryForBufferUnique (resourceHandle, allocationInfo);
            else
                return myAllocator->allocateMemoryForImageUnique (resourceHandle, allocationInfo);
        }

        template <Graphics::MemoryAccess Access, Graphics::MemoryPlacement Placement>
        [[nodiscard]] vma::UniqueAllocation allocate (vk::Image const image)
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

        [[nodiscard]] vma::Allocator get_allocator() const noexcept {
            return myAllocator.get();
        }

    private:
        vma::UniqueAllocator myAllocator;
    };

    template <bool InternalSync>
    class ResourceMemoryFactory :
        public virtual ResourceMemoryAllocator
    {
    protected:
        ResourceMemoryFactory () = default;

        template <Graphics::MemoryAccess Access,
                  Graphics::MemoryPlacement Placement,
                  class AllocateForHandle>
        requires (std::same_as <AllocateForHandle, vk::Buffer> ||
                  std::same_as <AllocateForHandle, vk::Image>)
        [[nodiscard]] vma::Allocation make_allocation (AllocateForHandle const resourceHandle)
        {
            auto allocation = ResourceMemoryAllocator::allocate <Access, Placement> (resourceHandle);

            [[maybe_unused]] auto const lock = lock_mutex <InternalSync, std::lock_guard> (myMutex);
            [[maybe_unused]] auto const [iter, inserted] = myAllocations.emplace (std::move (allocation));

            auto const allocationHandle = iter->get();
            return allocationHandle;
        }

        void destroy_allocation (vma::Allocation const allocation) noexcept(!InternalSync)
        {
            [[maybe_unused]] auto const lock =
                lock_mutex <InternalSync, std::lock_guard> (myMutex);

            myAllocations.erase (allocation);
        }

    private:
        [[no_unique_address]] EnableMutex <InternalSync, std::mutex> myMutex;

        alignas (SyncAlignment <InternalSync>)
        boost::unordered::unordered_flat_set
            <vma::UniqueAllocation, VulkanHash, VulkanEquals>
        myAllocations;
    };

    class ResourceMemoryDispatcher :
        public virtual ResourceMemoryAllocator
    {
    protected:
        ResourceMemoryDispatcher () = default;

        template <class Handle>
        requires (std::same_as <Handle, vk::raii::Buffer> ||
                  std::same_as <Handle, vk::raii::Image>)
        void bind_to_resource (Handle& resource, vma::Allocation const allocation)
        {
            auto const allocator = ResourceMemoryAllocator::get_allocator();
            auto const info = allocator.getAllocationInfo(allocation);

            resource.bindMemory (info.deviceMemory, info.offset);
        }

        [[nodiscard]] MemoryMapping
        map_memory (vma::Allocation const allocation) const
        {
            auto const allocator = get_allocator();

            if (auto const info = allocator.getAllocationInfo(allocation);
                info.pMappedData) [[unlikely]]
                return MemoryMapping { info.pMappedData };
            else
                return MemoryMapping { allocator, allocation };
        }

        void flush (
            vma::Allocation const allocation,
            size_t const size,
            size_t const offset) const
        {
            auto const allocator = ResourceMemoryAllocator::get_allocator();
            allocator.flushAllocation (allocation, offset, size);
        }
    };

    template <bool InternalSync>
    class ResourceMemoryManager :
        public virtual ResourceMemoryFactory <InternalSync>,
        public virtual ResourceMemoryDispatcher
    {};
}

#endif