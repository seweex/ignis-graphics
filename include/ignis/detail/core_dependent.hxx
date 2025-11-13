#ifndef IGNIS_DETAIL_CORE_DEPENDENT_HXX
#define IGNIS_DETAIL_CORE_DEPENDENT_HXX

#include <memory>
#include <ignis/graphics/core.hxx>

#include "ignis/graphics/buffer.hxx"

namespace Ignis::Detail
{
    class CoreDependent
    {
    protected:
        CoreDependent () noexcept {
            std::terminate();
        }

        explicit CoreDependent (std::weak_ptr <Graphics::Core> const& core) :
            myCore (core)
        {}

    public:
        [[nodiscard]] std::weak_ptr <Graphics::Core>
        get_core() const noexcept {
            return myCore;
        }

    protected:
        void assert_creation_thread () const noexcept {
            myCore->assert_creation_thread();
        }

        [[nodiscard]] uint32_t
        assert_passthrough_frames_number (uint32_t const frames) const
        {
            if (auto const capabilities = myCore->myPhysicalDevice.getSurfaceCapabilitiesKHR (myCore->mySurface);
                frames < capabilities.minImageCount || frames > capabilities.maxImageCount)
                [[unlikely]]
                    throw std::runtime_error("Unsupported frames number");

            return frames;
        }

        [[nodiscard]] vk::raii::Instance const&
        get_instance() const noexcept {
            return myCore->myInstance;
        }

        [[nodiscard]] vk::raii::PhysicalDevice const&
        get_physical_device() const noexcept {
            return myCore->myPhysicalDevice;
        }

        [[nodiscard]] vk::raii::Device const&
        get_device() const noexcept {
            return myCore->myDevice;
        }

        [[nodiscard]] FamilyAndQueueIndices const&
        get_indices () const noexcept {
            return myCore->myIndices;
        }

        [[nodiscard]] vk::SurfaceKHR
        get_surface () const noexcept {
            return *myCore->mySurface;
        }

        [[nodiscard]] vk::raii::detail::DeviceDispatcher const&
        get_device_dispatcher () const {
            return *myCore->myDevice.getDispatcher();
        }

        [[nodiscard]] vk::raii::detail::InstanceDispatcher const&
        get_instance_dispatcher () const {
            return *myCore->myInstance.getDispatcher();
        }

        [[nodiscard]] vk::raii::detail::ContextDispatcher const&
        get_context_dispatcher () const {
            return *myCore->myContext.getDispatcher();
        }

        [[nodiscard]] uint32_t
        get_vulkan_version () const {
            return myCore->get_vulkan_version();
        }

    private:
        std::shared_ptr <Graphics::Core> myCore;
    };
}

#endif