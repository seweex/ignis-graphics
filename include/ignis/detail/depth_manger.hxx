#ifndef IGNIS_DETAIL_DEPTH_MANGER_HXX
#define IGNIS_DETAIL_DEPTH_MANGER_HXX

#include <memory>

#include <boost/container/small_vector.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/hints.hxx>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/resource_memory.hxx>

namespace Ignis::Detail
{
    template <bool InternalSync>
    class DepthManager final :
        public DeviceDependent
    {
        [[nodiscard]] std::pair <vk::Format, vk::ImageTiling>
        pick_image_properties () const
        {
            std::array constexpr desiredFormats = {
                vk::Format::eD32Sfloat,
                vk::Format::eD16Unorm,
                vk::Format::eD32SfloatS8Uint,
                vk::Format::eD24UnormS8Uint
            };

            auto const& device = DeviceDependent::get_physical_device();

            auto estimateSuitability = [&] (
                vk::Format const format, bool const optimalRequired)
            {
                auto const properties = device.getFormatProperties(format);
                auto const features = optimalRequired ?
                    properties.optimalTilingFeatures : properties.linearTilingFeatures;

                return static_cast <bool> (features & vk::FormatFeatureFlagBits::eDepthStencilAttachment);
            };

            for (auto const format : desiredFormats)
                if (estimateSuitability (format, true))
                    return { format, vk::ImageTiling::eOptimal };

            for (auto const format : desiredFormats)
                if (estimateSuitability (format, false))
                    return { format, vk::ImageTiling::eLinear };

            throw std::runtime_error("None of depth formats are available");
        }

        [[nodiscard]] boost::container::small_vector <vk::raii::Image, Hints::images_count>
        make_images (
            vk::Format const format,
            vk::ImageTiling const tiling,
            vk::Extent2D const extent,
            uint32_t const count) const
        {
            boost::container::small_vector <vk::raii::Image, Hints::images_count> images;
            images.reserve (count);

            auto const& device = DeviceDependent::get_device();
            auto const graphicsFamily = DeviceDependent::get_indices().families.graphics;

            for (uint32_t i = 0; i < count; ++i)
            {
                vk::ImageCreateInfo const createInfo {
                    {},
                    vk::ImageType::e2D,
                    format,
                    vk::Extent3D { extent, 1 },
                    1,
                    1,
                    vk::SampleCountFlagBits::e1,
                    tiling,
                    vk::ImageUsageFlagBits::eDepthStencilAttachment,
                    vk::SharingMode::eExclusive,
                    1, &graphicsFamily,
                    vk::ImageLayout::eUndefined
                };

                images.emplace_back (device, createInfo);
            }

            return images;
        }

        [[nodiscard]] boost::container::small_vector <vma::Allocation, Hints::images_count>
        make_allocations (auto& images)
        {
            boost::container::small_vector <vma::Allocation, Hints::images_count> allocations;
            allocations.reserve (images.size());

            for (auto& image : images)
            {
                auto const allocation = myMemoryFactory.template make_allocation
                    <Graphics::MemoryAccess::unaccessible, Graphics::MemoryPlacement::device>
                    (*myMemoryAllocator, image);

                myMemoryManager.bind_to_resource (myMemoryAllocator->get_handle(), allocation, image);
                allocations.emplace_back (allocation);
            }

            return allocations;
        }

        [[nodiscard]] boost::container::small_vector <vk::raii::ImageView, Hints::images_count>
        make_views (vk::Format const format, auto const& images) const
        {
            boost::container::small_vector <vk::raii::ImageView, Hints::images_count> views;
            views.reserve (images.size());

            auto const& device = DeviceDependent::get_device();

            for (auto const& image : images)
            {
                vk::ImageViewCreateInfo const createInfo {
                    {},
                    image,
                    vk::ImageViewType::e2D,
                    format,
                    {},
                    { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 }
                };

                views.emplace_back (device, createInfo);
            }

            return views;
        }

        DepthManager (
            std::shared_ptr <ResourceMemoryAllocator>&& allocator,
            vk::Extent2D const extent,
            uint32_t const frames)
        :
            CoreDependent (allocator->get_core()),
            myMemoryAllocator (std::move (allocator)),
            myMemoryFactory ()
        {
            auto const [format, tiling] = pick_image_properties ();

            myFormat = format;

            myImages = make_images (format, tiling, extent, frames);
            myAllocations = make_allocations (myImages);
            myViews = make_views (format, myImages);
        }

    public:
        DepthManager (
            std::weak_ptr <ResourceMemoryAllocator> const& allocator,
            vk::Extent2D const extent,
            uint32_t const frames)
        :
            DepthManager (std::shared_ptr{ allocator }, extent, frames)
        {}

        DepthManager& operator=(DepthManager const&) = delete;
        DepthManager (DepthManager const&) = delete;

        DepthManager (DepthManager&&) noexcept = default;

        DepthManager& operator=(DepthManager&& other) noexcept
        {
            if (this == *other) [[unlikely]]
                return *this;

            reset();

            myMemoryAllocator = std::move (other.myMemoryAllocator);
            myMemoryFactory = std::move (other.myMemoryFactory);

            myImages = std::move (other.myImages);
            myAllocations = std::move (other.myAllocations);
            myViews = std::move (other.myViews);

            return *this;
        }

        ~DepthManager() noexcept {
            reset();
        }

        void reset () noexcept
        {
            if (is_valid()) [[likely]]
            {
                myViews.clear();

                for (auto const allocation : myAllocations)
                    myMemoryFactory.destroy_allocation (allocation);

                myAllocations.clear();
                myImages.clear();

                myMemoryAllocator.reset();
            }
        }

        [[nodiscard]] std::weak_ptr <ResourceMemoryAllocator>
        get_allocator () const noexcept {
            return myMemoryAllocator;
        }

        [[nodiscard]] vk::Format
        get_format () const noexcept {
            return myFormat;
        }

        [[nodiscard]] vk::ImageView
        get_view (uint32_t const frames) const noexcept {
            assert (is_valid());
            return myViews.at (frames);
        }

        [[nodiscard]] bool
        is_valid () const noexcept {
            return
                myMemoryAllocator &&
                !myImages.empty() &&
                !myViews.empty();
        }

    private:
        std::shared_ptr <ResourceMemoryAllocator> myMemoryAllocator;
        ResourceMemoryFactory <InternalSync> myMemoryFactory;
        [[no_unique_address]] ResourceMemoryManager myMemoryManager;

        vk::Format myFormat;

        boost::container::small_vector <vk::raii::Image, Hints::images_count> myImages;

        boost::container::small_vector <vma::Allocation, Hints::images_count> myAllocations;
        boost::container::small_vector <vk::raii::ImageView, Hints::images_count> myViews;

    };
}

#endif