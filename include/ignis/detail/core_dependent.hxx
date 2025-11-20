#ifndef IGNIS_DETAIL_CORE_DEPENDENT_HXX
#define IGNIS_DETAIL_CORE_DEPENDENT_HXX

#include <memory>
#include <ignis/graphics/core.hxx>

namespace Ignis::Detail
{
    class CoreDependent
    {
        template <class Ty, class... RestTys>
        [[nodiscard]] std::shared_ptr <Graphics::Core> get_common_core (
            Ty const& dependent,
            RestTys const&... rest)
        {
            std::shared_ptr <Graphics::Core> core { dependent.get_core() };

            if constexpr (sizeof...(RestTys) == 0)
                return core;

            else {
                if (core == get_common_core (rest...)) [[likely]]
                    return core;
                else
                    throw std::invalid_argument ("Dependents have different cores");
            }
        }

    protected:
        CoreDependent () noexcept {
            /* implicit initialization as a virtual base is not allowed */
            std::terminate();
        }

        explicit CoreDependent (std::weak_ptr <Graphics::Core> const& core) :
            myCore (core)
        {}

        template <std::derived_from <CoreDependent>... Tys>
            requires (sizeof... (Tys) > 1)
        explicit CoreDependent (Tys const&... dependents) :
            myCore (get_common_core (dependents...))
        {}

    public:
        CoreDependent (CoreDependent&&) noexcept = default;
        CoreDependent (CoreDependent const&) noexcept = default;

        CoreDependent& operator=(CoreDependent&&) noexcept = default;
        CoreDependent& operator=(CoreDependent const&) noexcept = default;

        [[nodiscard]] std::weak_ptr <Graphics::Core>
        get_core() const noexcept {
            return myCore;
        }

    protected:
        std::shared_ptr <Graphics::Core> myCore;
    };

    class VulkanApiDependent :
        public virtual CoreDependent
    {
    protected:
        VulkanApiDependent () noexcept = default;

        [[nodiscard]] vk::raii::Instance const&
        get_instance () const {
            return myCore->myInstance;
        }

        [[nodiscard]] vk::raii::detail::InstanceDispatcher const&
        get_instance_dispatcher () const {
            return *myCore->myInstance.getDispatcher();
        }

        [[nodiscard]] vk::raii::detail::DeviceDispatcher const&
        get_device_dispatcher () const {
            return *myCore->myDevice.getDispatcher();
        }

        [[nodiscard]] vk::raii::detail::ContextDispatcher const&
        get_context_dispatcher () const {
            return *myCore->myContext.getDispatcher();
        }

        [[nodiscard]] uint32_t get_vulkan_version () const noexcept {
            return myCore->get_vulkan_version();
        }
    };

    class DeviceDependent :
        public virtual CoreDependent
    {
    protected:
        DeviceDependent () noexcept = default;

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
    };
}

#endif