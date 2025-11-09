#ifndef IGNIS_DETAIL_TRANSFER_MANAGER_HXX
#define IGNIS_DETAIL_TRANSFER_MANAGER_HXX

#include <boost/unordered/unordered_flat_map.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/scheduler.hxx>

#include <ignis/graphics/buffer.hxx>

namespace Ignis::Detail
{
    template <bool InternalSync>
    class TransferManager :
        public virtual Detail::CoreDependent,
        public virtual Detail::ResourceMemoryDispatcher,
        public virtual Detail::TransferScheduler <InternalSync>
    {
        using ResourceMemoryDispatcher = Detail::ResourceMemoryDispatcher;
        using TransferScheduler = Detail::TransferScheduler <InternalSync>;

    protected:
        TransferManager () noexcept = default;

    public:
        template <Graphics::BufferType Type, Graphics::BufferUsage Usage>
        requires (Type == Graphics::BufferType::constantly_mapped ||
                  Type == Graphics::BufferType::temporary_mappable)
        void copy (
            Graphics::Buffer <Type, Usage> const destination,
            void const* const source,
            size_t const size,
            size_t const offset)
        {
            assert (source && size > 0);
            assert (destination.is_valid() && destination.mySize >= size + offset);

            auto const mapping = ResourceMemoryDispatcher::map_memory (destination.myMemory);
            auto const destPointer = static_cast <std::byte*> (mapping.get_pointer()) + offset;

            std::memcpy (destPointer, source, size);
            ResourceMemoryDispatcher::flush (destination.myMemory, size, offset);
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
            assert (source.is_valid() && destination.is_valid());

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
    };
}

#endif