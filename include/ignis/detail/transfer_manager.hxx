#ifndef IGNIS_DETAIL_TRANSFER_MANAGER_HXX
#define IGNIS_DETAIL_TRANSFER_MANAGER_HXX

#include <boost/unordered/unordered_flat_map.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/scheduler.hxx>

#include <ignis/graphics/buffer.hxx>
#include <ignis/graphics/image.hxx>

namespace Ignis::Detail
{
    template <bool InternalSync>
    class StagingBuffers final
    {
        using BufferFactory = Graphics::BufferFactory <false>;
        using Buffer = Graphics::Buffer
            <Graphics::BufferType::constantly_mapped, Graphics::BufferUsage::storage>;

        [[nodiscard]] Buffer
        emplace (size_t const size)
        {
            myBuffers.reserve (myBuffers.size() + 1);
            mySizeToIndex.reserve (mySizeToIndex.size() + 1);
            myOccupiedIndices.reserve (myOccupiedIndices.size() + 1);

            auto const buffer = myFactory->make_buffer <
                Graphics::BufferType::constantly_mapped,
                Graphics::BufferUsage::storage,
                Graphics::MemoryPlacement::no_matter> (size, true, false);

            auto const index = myBuffers.size();

            myBuffers.emplace_back (buffer);
            mySizeToIndex.emplace (buffer.get_size(), index);
            myOccupiedIndices.emplace (index);

            return buffer;
        }

    public:
        explicit StagingBuffers (std::weak_ptr <BufferFactory> const& factory) :
            myFactory (factory)
        {
            myOccupiedIndices.reserve (Hints::staged_transfers_per_frame);
        }

        StagingBuffers (StagingBuffers&&) = delete;
        StagingBuffers (StagingBuffers const&) = delete;

        StagingBuffers& operator=(StagingBuffers&&) = delete;
        StagingBuffers& operator=(StagingBuffers const&) = delete;

        [[nodiscard]] Buffer reserve (size_t const size)
        {
            [[maybe_unused]] auto const lock =
                lock_mutex <InternalSync, std::lock_guard> (myMutex);

            auto iter = mySizeToIndex.lower_bound (size);

            while (iter != mySizeToIndex.end() && myOccupiedIndices.contains(iter->second))
                ++iter;

            if (iter == mySizeToIndex.end()) [[unlikely]]
                /* place intentionally during the lock */
                return emplace (size);

            else {
                auto const index = iter->second;
                auto const buffer = myBuffers.at (index);

                myOccupiedIndices.emplace (index);
                return buffer;
            }
        }

        void reset () noexcept (!InternalSync)
        {
            [[maybe_unused]] auto const lock =
                lock_mutex <InternalSync, std::lock_guard> (myMutex);

            myOccupiedIndices.clear();
        }

        void cleanup (size_t const minCleaningSize = 0)
        {
            [[maybe_unused]] auto const lock =
                lock_mutex <InternalSync, std::lock_guard> (myMutex);

            assert (myOccupiedIndices.empty());

            boost::container::small_flat_set <size_t, Hints::staged_transfers_per_frame, std::greater <>>
            indicesToRemove;

            indicesToRemove.reserve (myBuffers.size());

            for (auto iter = mySizeToIndex.lower_bound (minCleaningSize);
                iter != mySizeToIndex.end();)
            {
                auto const index = iter->second;
                indicesToRemove.emplace (index);

                iter = mySizeToIndex.erase (iter);
            }

            auto const begin = myBuffers.begin();

            for (auto const index : indicesToRemove)
            {
                auto const buffer = myBuffers.at (index);
                myFactory->destroy_buffer (buffer);

                myBuffers.erase (begin + static_cast <ptrdiff_t> (index));
            }
        }

    private:
        std::shared_ptr <BufferFactory> myFactory;

        [[no_unique_address]] EnableMutex <InternalSync, std::mutex> myMutex;

        boost::container::small_vector <Buffer, Hints::staged_transfers_per_frame>
        myBuffers;

        boost::container::small_flat_multimap <size_t, size_t, Hints::staged_transfers_per_frame>
        mySizeToIndex;

        boost::unordered::unordered_flat_set <size_t>
        myOccupiedIndices;
    };
}

namespace Ignis::Graphics
{
    template <bool InternalSync>
    class TransferManager final :
        public Detail::DeviceDependent
    {
        using StagingBuffer = Buffer <BufferType::constantly_mapped, BufferUsage::storage>;

        template <BufferType Type, BufferUsage Usage>
        void flush_buffer (
            Buffer <Type, Usage> buffer,
            size_t const size,
            size_t const offset = 0)
        {
            auto const allocator = myMemoryAllocator->get_handle();
            auto const allocation = buffer.myMemory;

            myMemoryManager.flush_memory (allocator, allocation, size, offset);
        }

        [[nodiscard]] vk::BufferImageCopy
        make_copy_info (Image const& destination) const noexcept
        {
            vk::ImageSubresourceLayers constexpr subresourceLayers
                { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };

            auto const [width, height] = destination.get_sizes();
            vk::Extent3D const extent { width, height, 1 };

            return { 0, 0, 0, subresourceLayers, {}, extent };
        }

        [[nodiscard]] vk::BufferCopy
        make_copy_info (
            size_t const sourceOffset,
            size_t const destinationOffset,
            size_t const size) const noexcept
        {
            return { sourceOffset, destinationOffset, size };
        }

        [[nodiscard]] StagingBuffer
        prepare_staging_buffer (size_t const size, void const* const source)
        {
            auto const buffer = myStagingBuffers.reserve (size);
            auto const mapping = myMemoryManager.map_memory (buffer);

            auto const storage = mapping.get_pointer();
            std::memcpy (storage, source, size);

            flush_buffer (buffer, size);
            return buffer;
        }

    public:
        TransferManager (
            std::weak_ptr <Detail::TransferScheduler <InternalSync>> const& scheduler,
            std::weak_ptr <BufferFactory <InternalSync>> const& bufferFactory)
        :
            myScheduler (scheduler),
            myMemoryAllocator (bufferFactory.get_allocator()),
            myStagingBuffers (bufferFactory)
        {}

        TransferManager (TransferManager&&) = delete;
        TransferManager (TransferManager const&) = delete;

        TransferManager& operator=(TransferManager&&) = delete;
        TransferManager& operator=(TransferManager const&) = delete;

        template <BufferType Type, BufferUsage Usage>
            requires (Detail::is_enum_valid (Type) && Detail::is_enum_valid (Usage))
        void copy_to_buffer (
            Buffer <Type, Usage> destination,
            void const* const source,
            size_t const size,
            size_t const dstOffset = 0)
        {
            assert (destination.is_valid());
            assert (destination.get_size() >= size + dstOffset);

            assert (size > 0);
            assert (source);

            if constexpr (
                Type == Graphics::BufferType::constantly_mapped ||
                Type == Graphics::BufferType::temporary_mappable)
            {
                auto const mapping = myMemoryManager.map_memory
                    (myMemoryAllocator->get_handle(), destination.myMemory);

                auto const storage = mapping.get_pointer();
                auto const target = static_cast <std::byte *> (storage) + dstOffset;

                std::memcpy (target, source, size);
                flush_buffer (destination, size, dstOffset);
            }
            else /* staging */ {
                auto const staging = prepare_staging_buffer (size, source);
                auto const info = make_copy_info (0, dstOffset, size);

                [[maybe_unused]] auto [commands, lock] =
                    myScheduler->get_transfer_command_buffer();

                commands.copyBuffer (staging, destination.myBuffer, { 1, &info });
            }
        }

        template <BufferType DstType, BufferUsage DstUsage,
                  BufferType SrcType, BufferUsage SrcUsage>
        requires (Detail::is_enum_valid (DstUsage) &&
                  Detail::is_enum_valid (SrcType) &&
                  Detail::is_enum_valid (SrcUsage) &&
                  DstType == BufferType::transferable)
        void copy_to_buffer (
            Buffer <DstType, DstUsage> destination,
            Buffer <SrcType, SrcUsage> source,
            size_t const size,
            size_t const srcOffset = 0,
            size_t const dstOffset = 0)
        {
            assert (destination.is_valid());
            assert (destination.get_size() >= size + dstOffset);

            assert (source.is_valid());
            assert (source.get_size() >= size + srcOffset);

            assert (size > 0);

            auto const info = make_copy_info (srcOffset, dstOffset, size);

            [[maybe_unused]] auto [commands, lock] =
                myScheduler->get_transfer_command_buffer();

            commands.copyBuffer (source.myBuffer, destination.myBuffer, { 1, &info });
        }

        void copy_to_image (
            Image& destination,
            void const* const source,
            size_t const size)
        {
            assert (destination.is_valid());

            assert (size > 0);
            assert (source);

            auto const staging = prepare_staging_buffer (size, source);
            auto const info = make_copy_info (destination);

            [[maybe_unused]] auto [commands, lock] =
                myScheduler->get_transfer_command_buffer();

            commands.copyBufferToImage (staging, destination.myImage,
                vk::ImageLayout::eShaderReadOnlyOptimal, { 1, &info });
        }

        void cleanup_staging (size_t const minCleanupSize = 0) {
            myStagingBuffers.cleanup (minCleanupSize);
        }

        void reset_staging () {
            myStagingBuffers.reset();
        }

        [[nodiscard]] std::weak_ptr <Detail::TransferScheduler <InternalSync>>
        get_scheduler () const noexcept {
            return myScheduler;
        }

        [[nodiscard]] std::weak_ptr <Detail::ResourceMemoryAllocator>
        get_allocator () const noexcept {
            return myMemoryAllocator;
        }

    private:
        std::shared_ptr <Detail::TransferScheduler <InternalSync>> myScheduler;

        std::shared_ptr <Detail::ResourceMemoryAllocator> myMemoryAllocator;
        [[no_unique_address]] Detail::ResourceMemoryManager myMemoryManager;

        Detail::StagingBuffers <InternalSync> myStagingBuffers;
    };
}

#endif