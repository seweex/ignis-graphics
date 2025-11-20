#ifndef IGNIS_DETAIL_SWAPCHAIN_HXX
#define IGNIS_DETAIL_SWAPCHAIN_HXX

#include <memory>

#include <boost/container/small_vector.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/scheduler.hxx>

namespace Ignis::Detail
{
    struct ImageProperties
    {
        vk::Format format;
        vk::ColorSpaceKHR colorSpace;

        uint32_t images;
        vk::Extent2D extent;
        vk::PresentModeKHR presentMode;
    };

    class Swapchain final :
        public CreationThreadAsserter,
        public DeviceDependent
    {
        [[nodiscard]] vk::PresentModeKHR
        pick_present_mode (uint32_t const frames, bool const vsync) const
        {
            auto const& physicalDevice = DeviceDependent::get_physical_device();
            auto const surface = DeviceDependent::get_surface();

            auto const desiredModes =
                std::invoke ([&] () -> boost::container::small_vector <vk::PresentModeKHR, 4>
                {
                    if (frames == 1)
                        return { vk::PresentModeKHR::eImmediate };

                    else {
                        if (vsync) return {
                            vk::PresentModeKHR::eMailbox,
                            vk::PresentModeKHR::eFifo,
                            vk::PresentModeKHR::eFifoRelaxed,
                            vk::PresentModeKHR::eImmediate
                        };
                        else return {
                            vk::PresentModeKHR::eImmediate,
                            vk::PresentModeKHR::eFifoRelaxed,
                            vk::PresentModeKHR::eFifo,
                            vk::PresentModeKHR::eMailbox,
                        };
                    }
                });

            auto const availableModes = physicalDevice.getSurfacePresentModesKHR(surface);

            for (auto const mode : desiredModes)
                if (auto const iter = std::ranges::find (availableModes, mode);
                    iter != availableModes.end())
                    return mode;

            throw std::runtime_error("No suitable present modes are supported");
        }

        [[nodiscard]] std::pair <vk::Format, vk::ColorSpaceKHR>
        pick_format () const
        {
            auto const& physicalDevice = DeviceDependent::get_physical_device();
            auto const surface = DeviceDependent::get_surface();

            boost::container::small_vector <vk::Format, 15> srgbFormats;

            for (auto const [surfaceFormat, surfaceColorSpace] : physicalDevice.getSurfaceFormatsKHR (surface))
                if (surfaceColorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                    srgbFormats.emplace_back (surfaceFormat);

            std::array constexpr desiredFormats = {
                vk::Format::eB8G8R8A8Srgb,
                vk::Format::eR8G8B8A8Srgb,
                vk::Format::eR8G8B8A8Unorm,
                vk::Format::eB8G8R8A8Unorm
            };

            for (auto const desiredFormat : desiredFormats)
            {
                if (!(physicalDevice.getFormatProperties (desiredFormat).optimalTilingFeatures &
                    vk::FormatFeatureFlagBits::eColorAttachment)) [[unlikely]]
                    continue;

                if (auto const iter = std::ranges::find (srgbFormats, desiredFormat);
                    iter != srgbFormats.end())
                    return std::make_pair (desiredFormat, vk::ColorSpaceKHR::eSrgbNonlinear);
            }

            throw std::runtime_error("No suitable color format found");
        }

        [[nodiscard]] vk::Extent2D
        get_extent () const
        {
            auto const& physicalDevice = DeviceDependent::get_physical_device();
            auto const surface = DeviceDependent::get_surface();

            auto const capabilities = physicalDevice.getSurfaceCapabilitiesKHR (surface);
            return capabilities.currentExtent;
        }

        [[nodiscard]] ImageProperties
        pick_properties (uint32_t const frames, bool const vsync) const
        {
            auto const [colorFormat, colorSpace] = pick_format ();

            auto const extent = get_extent();
            auto const presentMode = pick_present_mode (frames, vsync);

            return {
                colorFormat,
                colorSpace,
                frames,
                extent,
                presentMode
            };
        }

        [[nodiscard]] vk::raii::SwapchainKHR
        make_swapchain (ImageProperties const& properties, vk::SwapchainKHR const old) const
        {
            auto const& device = DeviceDependent::get_device();
            auto const surface = DeviceDependent::get_surface();
            auto const families = DeviceDependent::get_indices().families;

            boost::container::small_flat_set <uint32_t, 2> const accessibleFamilies =
                { families.graphics, families.present };

            auto const& accessibleFamiliesView = accessibleFamilies.sequence();

            vk::SwapchainCreateInfoKHR const createInfo {
                {},
                surface,
                properties.images,
                properties.format,
                properties.colorSpace,
                properties.extent,
                1,
                vk::ImageUsageFlagBits::eColorAttachment,
                accessibleFamiliesView.size() > 1 ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
                static_cast<uint32_t>(accessibleFamiliesView.size()),
                accessibleFamiliesView.data(),
                vk::SurfaceTransformFlagBitsKHR::eIdentity,
                vk::CompositeAlphaFlagBitsKHR::eOpaque,
                properties.presentMode,
                true,
                old
            };

            return { device, createInfo };
        }

        [[nodiscard]] boost::container::small_vector <vk::Image, Hints::images_count>
        get_images (vk::raii::SwapchainKHR const& swapchain) const
        {
            auto const images = swapchain.getImages();

            boost::container::small_vector <vk::Image, Hints::images_count> result;
            result.reserve (images.size());

            for (auto const image : images)
                result.emplace_back(image);

            return result;
        }

        [[nodiscard]] boost::container::small_vector <vk::raii::ImageView, Hints::images_count>
        make_views (ImageProperties const& properties, auto const& images) const
        {
            auto const& device = DeviceDependent::get_device();

            boost::container::small_vector <vk::raii::ImageView, Hints::images_count> result;
            result.reserve (images.size());

            vk::ImageViewCreateInfo createInfo {
                {},
                VK_NULL_HANDLE,
                vk::ImageViewType::e2D,
                properties.format,
                {
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity
                },
                { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
            };

            for (auto const image : images) {
                createInfo.image = image;
                result.emplace_back(device, createInfo);
            }

            return result;
        }

    public:
        Swapchain (
            std::weak_ptr <Graphics::Core> const& core,
            uint32_t const frames,
            bool const vsync,
            vk::SwapchainKHR const oldSwapchain = VK_NULL_HANDLE)
        :
            CoreDependent (core),
            myProperties (pick_properties (frames, vsync)),
            mySwapchain (make_swapchain (myProperties, oldSwapchain)),
            myImages (get_images (mySwapchain)),
            myViews (make_views (myProperties, myImages))
        {}

        Swapchain (Swapchain&&) noexcept = default;
        Swapchain (Swapchain const&) = delete;

        Swapchain& operator=(Swapchain const&) = delete;

        Swapchain& operator=(Swapchain&& other) noexcept
        {
            if (this == &other) [[unlikely]]
                return *this;

            reset();

            myProperties = other.myProperties;
            mySwapchain = std::move (other.mySwapchain);
            myImages = std::move (other.myImages);
            myViews = std::move (other.myViews);

            return *this;
        }

        [[nodiscard]] vk::SwapchainKHR
        get_swapchain () const noexcept {
            assert (is_valid());
            return *mySwapchain;
        }

        [[nodiscard]] vk::ImageView
        get_view (uint32_t const frame) const noexcept {
            assert (is_valid());
            return myViews.at (frame);
        }

        [[nodiscard]] uint32_t
        get_images_count () const noexcept {
            return myProperties.images;
        }

        [[nodiscard]] vk::Format
        get_format () const noexcept {
            return myProperties.format;
        }

        [[nodiscard]] uint32_t
        get_next_frame (uint32_t const currentFrame) const noexcept {
            assert (is_valid());
            return (currentFrame + 1) % myProperties.images;
        }

        [[nodiscard]] uint32_t
        acquire_next_image (
            uint32_t const nextFrame,
            SchedulerBase& scheduler,
            SyncTools& syncTools)
        {
            CreationThreadAsserter::assert_creation_thread();
            assert (is_valid());

            auto constexpr timeout = std::numeric_limits <uint64_t>::max();

            auto const semaphore = syncTools.get_image_available_semaphore (nextFrame);
            auto const fence = syncTools.get_inflight_fence (nextFrame);

            scheduler.wait_fence (fence);

            auto const [result, nextImage] = mySwapchain.acquireNextImage (timeout, semaphore);

            if (result != vk::Result::eSuccess) [[unlikely]]
                throw std::runtime_error("Failed to acquire an image");

            return nextImage;
        }

        [[nodiscard]] bool
        is_valid () const noexcept {
            return
                mySwapchain != VK_NULL_HANDLE &&
                !myImages.empty() &&
                !myViews.empty();
        }

        void reset () noexcept
        {
            if (is_valid()) [[likely]]
            {
                myViews.clear();

                myImages.clear();
                mySwapchain.clear();
            }
        }

    private:
        ImageProperties myProperties;
        vk::raii::SwapchainKHR mySwapchain;

        boost::container::small_vector <vk::Image, Hints::images_count> myImages;
        boost::container::small_vector <vk::raii::ImageView, Hints::images_count> myViews;
    };
}

#endif