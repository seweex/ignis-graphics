#ifndef IGNIS_GRAPHICS_BUFFER_HXX
#define IGNIS_GRAPHICS_BUFFER_HXX

#include <type_traits>
#include <utility>
#include <mutex>

#include <boost/container/flat_set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <vulkan/vulkan_raii.hpp>
#include <ignis/detail/include_vulkan_allocator.hxx>

#include <ignis/detail/enable_sync.hxx>
#include <ignis/detail/vulkan_functional.hxx>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/resource_memory.hxx>
#include <ignis/detail/scheduler.hxx>

namespace Ignis::Detail
{
    template <bool InternalSync>
    class StagingBuffers;
}

namespace Ignis::Graphics
{
    enum class BufferType
    {
        transferable,
        temporary_mappable,
        constantly_mapped,

#if !NDEBUG
        first_enum_value = transferable,
        last_enum_value = constantly_mapped
#endif
    };

    enum class BufferUsage
    {
        vertex,
        index,
        uniform,
        storage,

#if !NDEBUG
        first_enum_value = vertex,
        last_enum_value = storage
#endif
    };

    template <BufferType Type, BufferUsage Usage>
    class Buffer final
    {
        Buffer (vma::Allocation const memory, vk::Buffer const buffer, size_t const size)
        noexcept :
            myMemory (memory),
            myBuffer (buffer),
            mySize (size)
        {}

    public:
        Buffer (Buffer const&) noexcept = default;
        Buffer& operator=(Buffer const&) noexcept = default;

        Buffer (Buffer&& other) noexcept :
            myMemory (std::exchange (other.myMemory, VK_NULL_HANDLE)),
            myBuffer (std::exchange (other.myBuffer, VK_NULL_HANDLE)),
            mySize (std::exchange (other.mySize, 0))
        {}

        Buffer& operator=(Buffer&& other) noexcept
        {
            if (this == &other) [[unlikely]]
                return *this;

            myMemory = std::exchange (other.myMemory, VK_NULL_HANDLE);
            myBuffer = std::exchange (other.myBuffer, VK_NULL_HANDLE);
            mySize = std::exchange (other.mySize, 0);

            return *this;
        }

        template <bool InternalSync>
        friend class Detail::StagingBuffers;

        template <bool InternalSync>
        friend class BufferFactory;

        template <bool InternalSync>
        friend class TransferManager;

        [[nodiscard]] bool
        is_valid () const noexcept {
            return
                myMemory &&
                myBuffer != VK_NULL_HANDLE &&
                mySize > 0;
        }

        [[nodiscard]] size_t
        get_size() const noexcept {
            assert (is_valid ());
            return mySize;
        }

    private:
        vma::Allocation myMemory;
        vk::Buffer myBuffer;

        size_t mySize;
    };

    template <bool InternalSync>
    class BufferFactory final :
        public Detail::DeviceDependent
    {
        template <BufferType Type>
        [[nodiscard]] static consteval MemoryAccess get_memory_access () noexcept
        {
            if constexpr (Type == BufferType::transferable)
                return MemoryAccess::transfer;

            else if constexpr (Type == BufferType::temporary_mappable)
                return MemoryAccess::temporary_mapped;

            else
                return MemoryAccess::constantly_mapped;
        }

        template <BufferUsage Usage>
        [[nodiscard]] static vk::BufferUsageFlags get_usage_flags (
            bool transferRead,
            bool transferWrite) noexcept
        {
            vk::BufferUsageFlags flags;

            if constexpr (Usage == BufferUsage::vertex)
                flags |= vk::BufferUsageFlagBits::eVertexBuffer;

            else if constexpr (Usage == BufferUsage::index)
                flags |= vk::BufferUsageFlagBits::eIndexBuffer;

            else if constexpr (Usage == BufferUsage::uniform)
                flags |= vk::BufferUsageFlagBits::eUniformBuffer;

            else /* BufferUsage::storage */
            {
                flags |= vk::BufferUsageFlagBits::eStorageBuffer;

                transferRead = true;
                transferWrite = true;
            }

            if (transferRead)
                flags |= vk::BufferUsageFlagBits::eTransferSrc;

            if (transferWrite)
                flags |= vk::BufferUsageFlagBits::eTransferDst;

            return flags;
        }

        template <BufferUsage Usage>
        [[nodiscard]] boost::container::small_flat_set <uint32_t, 2> get_accessible_families (
            bool const transferRead,
            bool const transferWrite) const noexcept
        {
            boost::container::small_flat_set <uint32_t, 2> accessibleFamilies;
            auto const coreFamilies = DeviceDependent::get_indices().families;

            if constexpr (Usage == BufferUsage::storage)
                accessibleFamilies.emplace (coreFamilies.transfer);
            else
                accessibleFamilies.emplace (coreFamilies.graphics);

            if (transferRead || transferWrite) [[likely]]
                accessibleFamilies.emplace (coreFamilies.transfer);

            return accessibleFamilies;
        }

        template <BufferUsage Usage>
        [[nodiscard]] vk::raii::Buffer create_buffer (
            size_t const size,
            bool const allowTransferRead,
            bool const allowTransferWrite)
        {
            auto const& device = DeviceDependent::get_device();

            auto const usage = get_usage_flags <Usage> (allowTransferRead, allowTransferWrite);
            auto const families = get_accessible_families <Usage> (allowTransferRead, allowTransferWrite);

            vk::BufferCreateInfo const createInfo {
                {},
                size,
                usage,
                families.size() > 1 ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
                static_cast <uint32_t> (families.sequence().size()),
                families.sequence().data()
            };

            return { device, createInfo };
        }

        explicit BufferFactory (std::shared_ptr <Detail::ResourceMemoryAllocator>&& allocator)
        :
            CoreDependent (allocator->get_core()),

            myMemoryAllocator (std::move (allocator)),
            myMemoryFactory (myMemoryAllocator->get_core())
        {}

    public:
        explicit BufferFactory (std::weak_ptr <Detail::ResourceMemoryAllocator> const& allocator) :
            BufferFactory (std::shared_ptr { allocator })
        {}

        BufferFactory (BufferFactory&&) = delete;
        BufferFactory (BufferFactory const&) = delete;

        BufferFactory& operator=(BufferFactory&&) = delete;
        BufferFactory& operator=(BufferFactory const&) = delete;

        template <BufferType Type,
                  BufferUsage Usage,
                  MemoryPlacement Placement>
        requires (Detail::is_enum_valid (Type) &&
                  Detail::is_enum_valid (Usage) &&
                  Detail::is_enum_valid (Placement))
        [[nodiscard]] Buffer <Type, Usage> make_buffer (
            size_t const size,
            bool const allowTransferRead,
            bool const allowTransferWrite)
        {
            assert (size > 0);

            if constexpr (Type == BufferType::transferable)
                assert (allowTransferWrite);

            else if constexpr (Usage == BufferUsage::storage)
                assert (allowTransferRead && allowTransferWrite);

            auto constexpr memory_access = get_memory_access <Type> ();

            auto buffer = create_buffer <Usage> (size, allowTransferRead, allowTransferWrite);
            auto memory = myMemoryFactory->template make_allocation <memory_access, Placement> (*buffer);

            myMemoryManager.bind_to_resource (buffer, memory);

            [[maybe_unused]] auto const lock =
                Detail::lock_mutex <InternalSync, std::lock_guard> (myMutex);

            auto const bufferHandle = **myBuffers.emplace (std::move(buffer)).first;
            return { memory, bufferHandle, size };
        }

        template <BufferType Type, BufferUsage Usage>
        void destroy_buffer (Buffer <Type, Usage> const buffer) noexcept (!InternalSync)
        {
            [[maybe_unused]] auto const lock =
               Detail::lock_mutex <InternalSync, std::lock_guard> (myMutex);

            myMemoryFactory->destroy_allocation (buffer.myMemory);
            myBuffers.erase (buffer.myBuffer);
        }

        [[nodiscard]] std::weak_ptr <Detail::ResourceMemoryAllocator>
        get_allocator () const noexcept {
            return myMemoryAllocator;
        }

    private:
        std::shared_ptr <Detail::ResourceMemoryAllocator> myMemoryAllocator;
        Detail::ResourceMemoryFactory <InternalSync> myMemoryFactory;
        [[no_unique_address]] Detail::ResourceMemoryManager myMemoryManager;

        [[no_unique_address]] Detail::EnableMutex <InternalSync, std::mutex> myMutex;

        boost::unordered::unordered_flat_set
            <vk::raii::Buffer, Detail::VulkanHash, Detail::VulkanEquals>
        myBuffers;
    };
}

#endif