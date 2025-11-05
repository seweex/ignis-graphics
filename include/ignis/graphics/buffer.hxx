#ifndef IGNIS_GRAPHICS_BUFFER_HXX
#define IGNIS_GRAPHICS_BUFFER_HXX

#include <type_traits>
#include <utility>
#include <mutex>

#include <boost/container/flat_set.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/sync_mocks.hxx>
#include <ignis/detail/vulkan_functional.hxx>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/resource_allocator.hxx>

namespace Ignis::Detail
{
    template <bool InternalSync>
    class BufferHandleStorage;

    template <>
    class BufferHandleStorage <true> final
    {
    public:
        [[nodiscard]] std::pair <vk::DeviceMemory, vk::Buffer>
        emplace (vk::raii::DeviceMemory&& memory, vk::raii::Buffer&& buffer)
        {
            std::lock_guard lock { myMutex };

            myMemory.reserve (myMemory.size() + 1);
            myBuffers.reserve (myBuffers.size() + 1);

            vk::DeviceMemory const memoryHandle = *myMemory.emplace (std::move (memory)).first;
            vk::Buffer const bufferHandle = *myBuffers.emplace (std::move (buffer)).first;

            return { memoryHandle, bufferHandle };
        }

        void destroy (vk::DeviceMemory const memory, vk::Buffer const buffer)
        {
            std::lock_guard lock { myMutex };

            if (0 != myMemory.erase(memory)) [[likely]]
                myBuffers.erase(buffer);
        }

    private:
        std::mutex myMutex;

        boost::unordered::unordered_flat_set <vk::raii::DeviceMemory, VulkanHash, VulkanEquals>
        myMemory;

        boost::unordered::unordered_flat_set <vk::raii::Buffer, VulkanHash, VulkanEquals>
        myBuffers;
    };

    template <>
    class BufferHandleStorage <false> final
    {
    public:
        [[nodiscard]] std::pair <vk::DeviceMemory, vk::Buffer>
        emplace (vk::raii::DeviceMemory&& memory, vk::raii::Buffer&& buffer)
        {
            myMemory.reserve (myMemory.size() + 1);
            myBuffers.reserve (myBuffers.size() + 1);

            vk::DeviceMemory const memoryHandle = *myMemory.emplace (std::move (memory)).first;
            vk::Buffer const bufferHandle = *myBuffers.emplace (std::move (buffer)).first;

            return { memoryHandle, bufferHandle };
        }

        void destroy (vk::DeviceMemory const memory, vk::Buffer const buffer) noexcept {
            if (0 != myMemory.erase(memory)) [[likely]]
                myBuffers.erase(buffer);
        }

    private:
        boost::unordered::unordered_flat_set <vk::raii::DeviceMemory, VulkanHash, VulkanEquals>
        myMemory;

        boost::unordered::unordered_flat_set <vk::raii::Buffer, VulkanHash, VulkanEquals>
        myBuffers;
    };
}

namespace Ignis::Graphics
{
    enum class BufferType
        { immutable, mappable, constantly_mapped };

    enum class BufferUsage
        { vertex, index, uniform, storage };

    template <BufferType Type, BufferUsage Usage>
    class Buffer final
    {
        Buffer (vk::DeviceMemory const memory, vk::Buffer const buffer)
        noexcept :
            myMemory (memory),
            myBuffer (buffer)
        {}

    public:
        template <bool InternalSync>
        friend class BufferFactory;

        template <bool InternalSync>
        friend class DataTransferDispatcher;

    private:
        vk::DeviceMemory myMemory;
        vk::Buffer myBuffer;
    };

    template <bool InternalSync>
    class BufferFactory :
        public virtual Detail::CoreDependent,
        public virtual Detail::ResourceAllocator
    {
        template <BufferType Type>
        [[nodiscard]] static consteval Detail::MemoryType choose_memory_type () noexcept
        {
            if constexpr (Type == BufferType::immutable)
                return Detail::MemoryType::immutable;
            else
                return Detail::MemoryType::mappable;
        }

        template <BufferUsage Usage>
        [[nodiscard]] static vk::BufferUsageFlags get_usage_flags (
            bool transferRead,
            bool transferWrite)
        {
            vk::BufferUsageFlags flags;

            if constexpr (Usage == BufferUsage::vertex)
                flags |= vk::BufferUsageFlagBits::eVertexBuffer;

            else if constexpr (Usage == BufferUsage::index)
                flags |= vk::BufferUsageFlagBits::eIndexBuffer;

            else if constexpr (Usage == BufferUsage::uniform)
                flags |= vk::BufferUsageFlagBits::eUniformBuffer;

            else /* Usage == BufferUsage::storage */
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
        [[nodiscard]] boost::container::small_flat_set <uint32_t, 2>
        get_accessible_families (
            bool const transferRead,
            bool const transferWrite) const noexcept
        {
            boost::container::small_flat_set <uint32_t, 2> accessibleFamilies;
            auto const coreFamilies = CoreDependent::get_indices().families;

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
            auto const& device = CoreDependent::get_device();

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

    protected:
        BufferFactory () noexcept = default;

    public:
        explicit BufferFactory (std::weak_ptr <Core> const& core) :
            CoreDependent (core)
        {}

        template <BufferType Type, BufferUsage Usage>
            requires ((
                Type == BufferType::mappable ||
                Type == BufferType::immutable ||
                Type == BufferType::constantly_mapped)
            &&
               (Usage == BufferUsage::vertex ||
                Usage == BufferUsage::index ||
                Usage == BufferUsage::uniform ||
                Usage == BufferUsage::storage))
        [[nodiscard]] Buffer <Type, Usage> make_buffer (
            size_t const size,
            bool const allowTransferRead,
            bool const allowTransferWrite)
        {
            assert (size > 0);

            if constexpr (Type == BufferType::immutable)
                assert (!allowTransferWrite);

            auto constexpr memoryType = choose_memory_type <Type> ();

            auto memory = ResourceAllocator::allocate <memoryType> (size);
            auto buffer = create_buffer <Usage> (size, allowTransferRead, allowTransferWrite);

            auto const [memoryHandle, bufferHandle] = myStorage.emplace (std::move (memory), std::move (buffer));

            return { memoryHandle, bufferHandle };
        }

        template <BufferType Type, BufferUsage Usage>
        void destroy_buffer (Buffer <Type, Usage> const buffer)
        noexcept (!InternalSync) {
            myStorage.destroy (buffer.myMemory, buffer.myBuffer);
        }

    private:
        Detail::BufferHandleStorage <InternalSync> myStorage;
    };
}

#endif