#ifndef IGNIS_GRAPHICS_IMAGE_HXX
#define IGNIS_GRAPHICS_IMAGE_HXX

#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_format_traits.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/resource_memory.hxx>

namespace Ignis::Graphics
{
    template <bool InternalSync>
    class TransferManager;

    enum class ImageFormat : std::underlying_type_t <vk::Format>
    {
        r8g8b8_srgb        = static_cast <std::underlying_type_t<vk::Format>> (vk::Format::eR8G8B8Srgb),
        r8g8b8a8_srgb      = static_cast <std::underlying_type_t<vk::Format>> (vk::Format::eR8G8B8A8Srgb),
        r8g8b8a8_uint_norm = static_cast <std::underlying_type_t<vk::Format>> (vk::Format::eR8G8B8A8Unorm),
        r32g32b32a32_float = static_cast <std::underlying_type_t<vk::Format>> (vk::Format::eR32G32B32A32Sfloat),

        r8_srgb      = static_cast <std::underlying_type_t<vk::Format>> (vk::Format::eR8Srgb),
        r8_uint_norm = static_cast <std::underlying_type_t<vk::Format>> (vk::Format::eR8Unorm),
        r16_float    = static_cast <std::underlying_type_t<vk::Format>> (vk::Format::eR16Sfloat),
        r32_float    = static_cast <std::underlying_type_t<vk::Format>> (vk::Format::eR32Sfloat),

#if !NDEBUG
        first_enum_value = r8g8b8_srgb,
        last_enum_value = r32_float
#endif
    };

    class Image final
    {
        Image (
            vma::Allocation const allocation,
            vk::Image const image,
            vk::ImageView const view,
            vk::Extent2D const sizes)
        noexcept :
            myAllocation (allocation),
            myImage (image),
            myView (view),
            mySizes (sizes)
        {}

    public:
        Image (Image const&) noexcept = default;
        Image& operator=(Image const&) noexcept = default;

        Image (Image&& other) noexcept :
            myAllocation (std::exchange (other.myAllocation, VK_NULL_HANDLE)),
            myImage (std::exchange (other.myImage, VK_NULL_HANDLE)),
            myView (std::exchange (other.myView, VK_NULL_HANDLE)),
            mySizes (std::exchange (other.mySizes, vk::Extent2D{ 0, 0 }))
        {}

        Image& operator=(Image&& other) noexcept
        {
            if (this == &other) [[unlikely]]
                return *this;

            myAllocation = std::exchange (other.myAllocation, VK_NULL_HANDLE);
            myImage = std::exchange (other.myImage, VK_NULL_HANDLE);
            myView = std::exchange (other.myView, VK_NULL_HANDLE);
            mySizes = std::exchange (other.mySizes, vk::Extent2D{ 0, 0 });

            return *this;
        }

        template <bool InternalSync>
        friend class ImageFactory;

        template <bool InternalSync>
        friend class TransferManager;

        [[nodiscard]] bool
        is_valid () const noexcept {
            return
                myAllocation &&
                myImage &&
                myView &&
                mySizes != vk::Extent2D{ 0, 0 };
        }

        [[nodiscard]] std::pair <uint32_t, uint32_t>
        get_sizes () const noexcept {
            assert (is_valid());
            return { mySizes.width, mySizes.height };
        }

    private:
        vma::Allocation myAllocation;
        vk::Image myImage;
        vk::ImageView myView;

        vk::Extent2D mySizes;
    };

    template <bool InternalSync>
    class ImageFactory final :
        public virtual Detail::DeviceDependent
    {
        [[nodiscard]] std::optional <vk::ImageTiling>
        pick_tiling (vk::Format const format) const
        {
            auto constexpr required_features =
                vk::FormatFeatureFlagBits::eSampledImage |
                vk::FormatFeatureFlagBits::eSampledImageFilterLinear |
                vk::FormatFeatureFlagBits::eTransferDst;

            auto const& device = DeviceDependent::get_physical_device();
            auto const properties = device.getFormatProperties (format);

            if ((properties.optimalTilingFeatures & required_features) == required_features)
                return vk::ImageTiling::eOptimal;

            else if ((properties.linearTilingFeatures & required_features) == required_features)
                return vk::ImageTiling::eLinear;

            return std::nullopt;
        }

        [[nodiscard]] bool
        is_resolution_supported (
            uint32_t const width,
            uint32_t const height) const
        {
            auto const& device = DeviceDependent::get_physical_device();
            auto const limits = device.getProperties().limits;

            return width > 0 && height > 0 &&
                width <= limits.maxImageDimension2D &&
                height <= limits.maxImageDimension2D;
        }

        [[nodiscard]] vk::ComponentMapping
        make_mapping (vk::Format const format) const
        {
            auto const components = vk::componentCount(format);

            return {
                vk::ComponentSwizzle::eIdentity,
                components >= 2 ? vk::ComponentSwizzle::eIdentity : vk::ComponentSwizzle::eZero,
                components >= 3 ? vk::ComponentSwizzle::eIdentity : vk::ComponentSwizzle::eZero,
                components >= 4 ? vk::ComponentSwizzle::eIdentity : vk::ComponentSwizzle::eOne
            };
        }

        [[nodiscard]] vk::raii::Image
        create_image (
            vk::Format const format,
            uint32_t const width,
            uint32_t const height) const
        {
            auto const& device = DeviceDependent::get_device();
            auto const families = DeviceDependent::get_indices().families;

            boost::container::small_flat_set <uint32_t, 2> const
                accessibleFamilies = { families.graphics, families.transfer };

            auto const tiling = pick_tiling (format);

            if (!tiling.has_value()) [[unlikely]]
                throw std::runtime_error ("Not a suitable format");

            vk::ImageCreateInfo const createInfo
            {
                {},
                vk::ImageType::e2D,
                format,
                vk::Extent3D { width, height, 1 },
                1,
                1,
                vk::SampleCountFlagBits::e1,
                *tiling,
                vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eTransferDst,
                accessibleFamilies.size() > 1 ?
                    vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
                static_cast <uint32_t> (accessibleFamilies.sequence().size()),
                accessibleFamilies.sequence().data(),
                vk::ImageLayout::eTransferDstOptimal
            };

            return { device, createInfo };
        }

        [[nodiscard]] vk::raii::ImageView
        create_view (
            vk::Image const image,
            vk::Format const format) const
        {
            auto const& device = DeviceDependent::get_device();
            auto const mapping = make_mapping (format);

            vk::ImageViewCreateInfo const createInfo
            {
                {},
                image,
                vk::ImageViewType::e2D,
                format,
                mapping,
                vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor,
                    0, 1, 0, 1
                }
            };

            return { device, createInfo };
        }

        explicit ImageFactory (std::shared_ptr <Detail::ResourceMemoryAllocator>&& allocator)
        :
            CoreDependent (allocator->get_core()),

            myMemoryAllocator (std::move (allocator)),
            myMemoryFactory (myMemoryAllocator->get_core())
        {}

    public:
        explicit ImageFactory (std::weak_ptr <Detail::ResourceMemoryAllocator> const& allocator) :
            ImageFactory (std::shared_ptr { allocator })
        {}

        ImageFactory (ImageFactory&&) = delete;
        ImageFactory (ImageFactory const&) = delete;

        ImageFactory& operator=(ImageFactory&&) = delete;
        ImageFactory& operator=(ImageFactory const&) = delete;

        [[nodiscard]] Image
        make_image (
            ImageFormat const format,
            uint32_t const width,
            uint32_t const height)
        {
            assert (Detail::is_enum_valid(format));
            assert (width > 0 && height > 0);

            if (!is_resolution_supported (width, height)) [[unlikely]]
                throw std::runtime_error ("Unsupported image sizes");

            auto const vulkanFormat = static_cast <vk::Format> (format);

            auto image = create_image (vulkanFormat, width, height);
            auto view = create_view (image, vulkanFormat);

            auto allocation = myMemoryFactory.template make_allocation
                <MemoryAccess::transfer, MemoryPlacement::device> (image);

            myMemoryManager.bind_to_resource (image, allocation);

            [[maybe_unused]] auto const lock =
                Detail::lock_mutex <InternalSync, std::unique_lock> (myMutex);

            myImages.reserve (myImages.size() + 1);
            myViews.reserve (myViews.size() + 1);

            [[maybe_unused]] auto const [imageIter, imageInserted] =
                myImages.emplace (std::move (image));

            [[maybe_unused]] auto const [viewIter, viewInserted] =
                myViews.emplace (std::move (view));

            return Image { *imageIter, *viewIter, allocation };
        }

        void destroy_image (Image const& image) noexcept (!InternalSync)
        {
            if (image.is_valid()) [[likely]]
            {
                [[maybe_unused]] auto const lock =
                    Detail::lock_mutex <InternalSync, std::unique_lock> (myMutex);

                myImages.erase (image.myImage);
                myViews.erase (image.myView);
            }
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

        alignas (Detail::SyncAlignment <InternalSync>)
        boost::unordered::unordered_flat_set
            <vk::raii::Image, Detail::VulkanHash, Detail::VulkanEquals>
        myImages;

        alignas (Detail::SyncAlignment <InternalSync>)
        boost::unordered::unordered_flat_set
            <vk::raii::ImageView, Detail::VulkanHash, Detail::VulkanEquals>
        myViews;
    };
}

#endif