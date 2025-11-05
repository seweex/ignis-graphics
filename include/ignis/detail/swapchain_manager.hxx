#ifndef IGNIS_DETAIL_SWAPCHAIN_MANAGER_HXX
#define IGNIS_DETAIL_SWAPCHAIN_MANAGER_HXX

#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/scheduler.hxx>

namespace Ignis::Detail
{
    struct ImageProperties
    {
        vk::Format colorFormat;
        vk::ColorSpaceKHR colorSpace;

        vk::Format depthFormat;
        vk::PresentModeKHR presentMode;

        uint32_t images;
        vk::Extent2D extent;
    };

    class SwapchainManager :
        public virtual CoreDependent,
        public virtual SchedulerBase
    {
        [[nodiscard]] static vk::PresentModeKHR
        pick_present_mode (
            vk::raii::PhysicalDevice const& physicalDevice,
            vk::SurfaceKHR const surface,
            uint32_t const frames,
            bool const vsync)
        {
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

        [[nodiscard]] static std::pair <vk::Format, vk::ColorSpaceKHR>
        pick_color_format (
            vk::raii::PhysicalDevice const& physicalDevice,
            vk::SurfaceKHR const surface)
        {
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
                if (auto const iter = std::ranges::find (srgbFormats, desiredFormat);
                    iter != srgbFormats.end())
                    return std::make_pair (desiredFormat, vk::ColorSpaceKHR::eSrgbNonlinear);

            throw std::runtime_error("No suitable color format found");
        }

        [[nodiscard]] static vk::Format
        pick_depth_format (vk::raii::PhysicalDevice const& physicalDevice)
        {
            std::array constexpr desiredFormats = {
                vk::Format::eD32Sfloat,
                vk::Format::eD32SfloatS8Uint,
                vk::Format::eX8D24UnormPack32,
                vk::Format::eD16Unorm,
                vk::Format::eD24UnormS8Uint,
                vk::Format::eD16UnormS8Uint
            };

            for (auto const desiredFormat : desiredFormats)
                if (physicalDevice.getFormatProperties (desiredFormat).optimalTilingFeatures
                    & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
                    return desiredFormat;

            throw std::runtime_error("No suitable depth format found");
        }

        [[nodiscard]] static vk::Extent2D
        get_extent (
            vk::raii::PhysicalDevice const& physicalDevice,
            vk::SurfaceKHR const surface)
        {
            auto const capabilities = physicalDevice.getSurfaceCapabilitiesKHR (surface);
            return capabilities.currentExtent;
        }

        [[nodiscard]] static ImageProperties
        pick_properties (
            vk::raii::PhysicalDevice const& physicalDevice,
            vk::SurfaceKHR const surface,
            uint32_t const frames,
            bool const vsync)
        {
            auto const [colorFormat, colorSpace] = pick_color_format (physicalDevice, surface);

            return {
                colorFormat,
                colorSpace,
                pick_depth_format (physicalDevice),
                pick_present_mode (physicalDevice, surface, frames, vsync),
                frames,
                get_extent (physicalDevice, surface)
            };
        }

        [[nodiscard]] static vk::raii::SwapchainKHR
        make_swapchain (
            vk::raii::Device const& device,
            vk::SurfaceKHR const surface,
            QueueIndices const families,
            ImageProperties const& properties,
            vk::SwapchainKHR const old = VK_NULL_HANDLE)
        {
            boost::container::small_flat_set <uint32_t, 2> const accessibleFamilies =
                { families.graphics, families.present };

            auto const& accessibleFamiliesView = accessibleFamilies.sequence();

            vk::SwapchainCreateInfoKHR const createInfo {
                {},
                surface,
                properties.images,
                properties.colorFormat,
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

        [[nodiscard]] static boost::container::small_vector <vk::Image, SchedulerBase::images_hint>
        get_images (vk::raii::SwapchainKHR const& swapchain)
        {
            auto const images = swapchain.getImages();

            boost::container::small_vector <vk::Image, SchedulerBase::images_hint> result;
            result.reserve (images.size());

            for (auto const image : images)
                result.emplace_back(image);

            return result;
        }

        [[nodiscard]] static boost::container::small_vector <vk::raii::ImageView, SchedulerBase::images_hint>
        make_views (
            vk::raii::Device const& device,
            ImageProperties const& properties,
            auto const& images)
        {
            boost::container::small_vector <vk::raii::ImageView, SchedulerBase::images_hint> result;
            result.reserve (images.size());

            vk::ImageViewCreateInfo createInfo {
                {},
                VK_NULL_HANDLE,
                vk::ImageViewType::e2D,
                properties.colorFormat,
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
    
    protected:
        SwapchainManager (uint32_t const frames, bool const vsync)
        :        
            myProperties (pick_properties (CoreDependent::get_physical_device(),
                CoreDependent::get_surface(), frames, vsync)),

            mySwapchain (make_swapchain (CoreDependent::get_device(), CoreDependent::get_surface(),
                CoreDependent::get_indices().families, myProperties)),

            myImages (get_images (mySwapchain)),
            myViews  (make_views (CoreDependent::get_device(), myProperties, myImages))
        {}

        [[nodiscard]] ImageProperties
        get_image_properties() const noexcept {
            return myProperties;
        }

        [[nodiscard]] vk::SwapchainKHR
        get_swapchain () const noexcept {
            return *mySwapchain;
        }

        [[nodiscard]] uint32_t
        get_next_frame (uint32_t const currentFrame) const noexcept {
            return (currentFrame + 1) % myProperties.images;
        }

        [[nodiscard]] uint32_t
        acquire_next_image (uint32_t const nextFrame)
        {
            CoreDependent::assert_creation_thread();

            auto constexpr timeout = std::numeric_limits <uint64_t>::max();

            auto const semaphore = SchedulerBase::get_image_available_semaphore (nextFrame);
            auto const fence = SchedulerBase::get_inflight_fence (nextFrame);

            SchedulerBase::wait_fence (fence);

            auto const [result, nextImage] = mySwapchain.acquireNextImage (timeout, semaphore);

            if (result != vk::Result::eSuccess) [[unlikely]]
                throw std::runtime_error("Failed to acquire an image");

            return nextImage;
        }

        [[nodiscard]] boost::container::small_vector <vk::raii::Framebuffer, SchedulerBase::images_hint>
        make_framebuffers (vk::RenderPass const renderPass)
        {
            boost::container::small_vector <vk::raii::Framebuffer, SchedulerBase::images_hint> framebuffers;
            framebuffers.reserve (myViews.size());

            auto const& device = CoreDependent::get_device();

            for (auto const& view : myViews)
            {
                vk::FramebufferCreateInfo const createInfo {
                    {},
                    renderPass,
                    1,
                    &*view,
                    myProperties.extent.width,
                    myProperties.extent.height,
                    1
                };

                framebuffers.emplace_back (device, createInfo);
            }

            return framebuffers;
        }

    private:
        ImageProperties myProperties;
        vk::raii::SwapchainKHR mySwapchain;

        boost::container::small_vector <vk::Image, SchedulerBase::images_hint> myImages;
        boost::container::small_vector <vk::raii::ImageView, SchedulerBase::images_hint> myViews;
    };
}

#endif