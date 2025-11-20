#ifndef IGNIS_DETAIL_RESOURCE_MEMORY_HXX
#define IGNIS_DETAIL_RESOURCE_MEMORY_HXX

#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <ignis/detail/include_vulkan_allocator.hxx>
#include <ignis/detail/vulkan_functional.hxx>

#include <ignis/detail/core_dependent.hxx>

namespace Ignis::Graphics
{
    enum class MemoryAccess
    {
        unaccessible,
        transfer,
        temporary_mapped,
        constantly_mapped,

#if !NDEBUG
        first_enum_value = unaccessible,
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

        explicit MemoryMapping (void* const mapped)
        noexcept :
            myAllocator (nullptr),
            myAllocation (nullptr),
            myPointer (mapped)
        {}

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

        ~MemoryMapping() noexcept
        { release(); }

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

    class ResourceMemoryAllocator final :
        public VulkanApiDependent,
        public DeviceDependent
    {
        [[nodiscard]] vma::UniqueAllocator
        make_allocator () const
        {
            auto const& instance = VulkanApiDependent::get_instance();
            auto const version = VulkanApiDependent::get_vulkan_version();

            auto const& deviceDispatcher = VulkanApiDependent::get_device_dispatcher();
            auto const& instanceDispatcher = VulkanApiDependent::get_instance_dispatcher();
            auto const& contextDispatcher = VulkanApiDependent::get_context_dispatcher();

            auto const& physicalDevice = DeviceDependent::get_physical_device();
            auto const& device = DeviceDependent::get_device();

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
                physicalDevice,
                device,
                {},
                nullptr,
                nullptr,
                nullptr,
                &vulkanFunctions,
                instance,
                version,
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

        template <Graphics::MemoryPlacement Placement>
        [[nodiscard]] static constexpr vma::MemoryUsage make_usage_flags () noexcept
        {

                return {};
        }

    public:
        explicit ResourceMemoryAllocator (std::weak_ptr <Graphics::Core> const& core) :
            CoreDependent (core),
            myAllocator (make_allocator ())
        {}

        ResourceMemoryAllocator (ResourceMemoryAllocator &&) noexcept = default;
        ResourceMemoryAllocator (ResourceMemoryAllocator const&) = delete;

        ResourceMemoryAllocator& operator=(ResourceMemoryAllocator &&) noexcept = default;
        ResourceMemoryAllocator& operator=(ResourceMemoryAllocator const&) = delete;

        template <Graphics::MemoryAccess Access,
                  Graphics::MemoryPlacement Placement,
                  class Handle>
        requires (std::convertible_to <Handle, vk::Buffer> ||
                  std::convertible_to <Handle, vk::Image>)
        [[nodiscard]] vma::UniqueAllocation allocate (Handle const& resourceHandle)
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

            if constexpr (std::convertible_to <Handle, vk::Buffer>)
                return myAllocator->allocateMemoryForBufferUnique (resourceHandle, allocationInfo);
            else
                return myAllocator->allocateMemoryForImageUnique (resourceHandle, allocationInfo);
        }

        [[nodiscard]] vma::Allocator
        get_handle () const noexcept {
            return myAllocator.get();
        }

    private:
        vma::UniqueAllocator myAllocator;
    };

    template <bool InternalSync>
    class ResourceMemoryFactory
    {
    public:
        ResourceMemoryFactory () noexcept = default;

        ResourceMemoryFactory (ResourceMemoryFactory &&) = delete;
        ResourceMemoryFactory (ResourceMemoryFactory const&) = delete;

        ResourceMemoryFactory& operator=(ResourceMemoryFactory &&) = delete;
        ResourceMemoryFactory& operator=(ResourceMemoryFactory const&) = delete;

        template <Graphics::MemoryAccess Access,
                  Graphics::MemoryPlacement Placement,
                  class Handle>
        requires (std::convertible_to <Handle, vk::Buffer> ||
                  std::convertible_to <Handle, vk::Image>)
        [[nodiscard]] vma::Allocation make_allocation (
            ResourceMemoryAllocator& allocator,
            Handle const& resourceHandle)
        {
            auto allocation = allocator.allocate <Access, Placement> (resourceHandle);

            [[maybe_unused]] auto const lock =
                lock_mutex <InternalSync, std::lock_guard> (myMutex);

            [[maybe_unused]] auto const [iter, inserted] =
                myAllocations.emplace (std::move (allocation));

            return iter->get();
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

    class ResourceMemoryManager
    {
    public:
        ResourceMemoryManager () noexcept = default;

        template <class Handle>
        requires (std::same_as <Handle, vk::raii::Buffer> ||
                  std::same_as <Handle, vk::raii::Image>)
        void bind_to_resource (
            vma::Allocator const allocator,
            vma::Allocation const allocation,
            Handle& resource) const
        {
            auto const info = allocator.getAllocationInfo (allocation);
            resource.bindMemory (info.deviceMemory, info.offset);
        }

        void flush_memory (
            vma::Allocator const allocator,
            vma::Allocation const allocation,
            size_t const size,
            size_t const offset) const
        {
            allocator.flushAllocation (allocation, offset, size);
        }

        [[nodiscard]] MemoryMapping
        map_memory (
            vma::Allocator const allocator,
            vma::Allocation const allocation) const
        {
            if (auto const info = allocator.getAllocationInfo(allocation);
                info.pMappedData) [[unlikely]]
                return MemoryMapping { info.pMappedData };
            else
                return MemoryMapping { allocator, allocation };
        }
    };
}

#endif