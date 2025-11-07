#ifndef IGNIS_DETAIL_TRANSFER_MANAGER_HXX
#define IGNIS_DETAIL_TRANSFER_MANAGER_HXX

#include <boost/unordered/unordered_flat_map.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/scheduler.hxx>

#include <ignis/graphics/buffer.hxx>

namespace Ignis::Detail
{
    class MemoryMapping final
    {
    public:
        MemoryMapping (
            size_t const size,
            size_t const offset,
            vk::Device const device,
            vk::DeviceMemory const memory,
            vk::raii::detail::DeviceDispatcher const* dispatcher)
        :
            myDispatcher (dispatcher),
            myDevice (device),
            myMemory (memory)
        {
            myPointer = myDevice.mapMemory
                (myMemory, offset, size, {}, *dispatcher);
        }

        ~MemoryMapping() noexcept {
            release();
        }

        MemoryMapping (MemoryMapping&& other)
        noexcept :
            myDispatcher (std::exchange (other.myDispatcher, nullptr)),
            myDevice (std::exchange (other.myDevice, VK_NULL_HANDLE)),
            myMemory (std::exchange (other.myMemory, VK_NULL_HANDLE)),
            myPointer (std::exchange (other.myPointer, nullptr))
        {}

        MemoryMapping& operator=(MemoryMapping&& other) noexcept
        {
            if (this == &other) [[unlikely]]
                return *this;

            release();

            myDispatcher = std::exchange (other.myDispatcher, nullptr);
            myDevice = std::exchange (other.myDevice, VK_NULL_HANDLE);
            myMemory = std::exchange (other.myMemory, VK_NULL_HANDLE);
            myPointer = std::exchange (other.myPointer, nullptr);

            return *this;
        }

        MemoryMapping (MemoryMapping const&) = delete;
        MemoryMapping& operator=(MemoryMapping const&) = delete;

        [[nodiscard]] bool owns_mapping () const noexcept {
            return myPointer != nullptr;
        }

        [[nodiscard]] void* get_pointer () const noexcept {
            assert (owns_mapping());
            return myPointer;
        }

        void release () noexcept {
            if (owns_mapping()) {
                myDevice.unmapMemory (myMemory, *myDispatcher);
                myPointer = nullptr;
            }
        }

    private:
        vk::raii::detail::DeviceDispatcher const* myDispatcher;

        vk::Device myDevice;
        vk::DeviceMemory myMemory;

        void* myPointer;
    };

    template <bool InternalSync>
    class TransferManager :
        public virtual Detail::CoreDependent,
        public virtual Detail::TransferScheduler <InternalSync>
    {
        using TransferScheduler = Detail::TransferScheduler <InternalSync>;

        [[nodiscard]] void*
        find_constantly_mapped (vk::DeviceMemory const memory) noexcept (!InternalSync)
        {
            [[maybe_unused]] auto const lock = Detail::lock_mutex
                <InternalSync, std::shared_lock> (myConstantlyMappingsMutex);

            if (auto const iter = myConstantlyMappings.find (memory);
                iter != myConstantlyMappings.end()) [[likely]]
                return iter->second.get_pointer();
            else
                return nullptr;
        }

        [[nodiscard]] void*
        emplace_constantly_mapped (vk::DeviceMemory const memory, size_t const size)
        {
            auto const& device = CoreDependent::get_device();
            auto const dispatcher = &CoreDependent::get_dispatcher();

            [[maybe_unused]] auto const lock = Detail::lock_mutex
                <InternalSync, std::unique_lock> (myConstantlyMappingsMutex);

            [[maybe_unused]] auto const [iter, inserted] = myConstantlyMappings.emplace(
                std::piecewise_construct,
                std::forward_as_tuple (memory),
                std::forward_as_tuple (size, 0, device, memory, dispatcher));

            auto const& mapping = iter->second;
            return mapping.get_pointer();
        }

        [[nodiscard]] Detail::MemoryMapping
        make_temporary_mapping (
            vk::DeviceMemory const memory,
            size_t const size,
            size_t const offset) const
        {
            auto const device = *CoreDependent::get_device();
            auto const dispatcher = &CoreDependent::get_dispatcher();

            return Detail::MemoryMapping { size, offset, device, memory, dispatcher };
        }

    protected:
        TransferManager () noexcept = default;

    public:
        template <Graphics::BufferType Type, Graphics::BufferUsage Usage>
        requires (
            Type == Graphics::BufferType::constantly_mapped ||
            Type == Graphics::BufferType::immutable)
        void copy (
            Graphics::Buffer <Type, Usage> const destination,
            void const* const source,
            size_t const size,
            size_t const offset)
        {
            assert (size > 0);

            assert (destination.is_valid());
            assert (destination.mySize >= size + offset);

            if constexpr (Type == Graphics::BufferType::constantly_mapped)
            {
                auto pointer = find_constantly_mapped (destination.myMemory);

                if (!pointer) [[unlikely]]
                    pointer = emplace_constantly_mapped (destination.myMemory, destination.mySize);

                std::memcpy (pointer, source, size);
            }
            else {
                auto const mapping = make_temporary_mapping (destination.myMemory, size, offset);
                auto const pointer = mapping.get_pointer();

                std::memcpy (pointer, source, size);
            }
        }

        template <Graphics::BufferType DestType,
                  Graphics::BufferUsage DestUsage,
                  Graphics::BufferType SrcType,
                  Graphics::BufferUsage SrcUsage>
        void copy (
            Graphics::Buffer <DestType, DestUsage> const destination,
            Graphics::Buffer <SrcType, SrcUsage> const source,
            size_t const size,
            size_t const destinationOffset,
            size_t const sourceOffset)
        {
            assert (size > 0);

            assert (source.is_valid());
            assert (destination.is_valid());

            assert (source.mySize >= size + sourceOffset);
            assert (destination.mySize >= size + destinationOffset);

            auto const commands = TransferScheduler::make_transfer_command_buffers ();

            vk::CommandBufferBeginInfo constexpr beginInfo
                { vk::CommandBufferUsageFlagBits::eOneTimeSubmit };

            commands.begin (beginInfo);

            vk::BufferCopy const copyInfo { sourceOffset, destinationOffset, size };
            commands.copyBuffer (source.myBuffer, destination.myBuffer, { 1, &copyInfo });

            commands.end();
            TransferScheduler::postpone_transfer_commands (std::move (commands));
        }

    private:
        [[no_unique_address]] Detail::EnableMutex <InternalSync, std::shared_mutex>
        mutable myConstantlyMappingsMutex;

        alignas (InternalSync ? 64 : alignof(void*)) boost::unordered_flat_map
            <vk::DeviceMemory, Detail::MemoryMapping,
            Detail::VulkanHash, Detail::VulkanEquals>
        myConstantlyMappings;
    };
}

#endif