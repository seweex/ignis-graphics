#ifndef IGNIS_DETAIL_CORE_DEPENDENT_HXX
#define IGNIS_DETAIL_CORE_DEPENDENT_HXX

#include <mutex>
#include <memory>
#include <thread>

#include <ignis/graphics/core.hxx>

namespace Ignis::Detail
{
    class CoreDependent
    {
    protected:
        explicit CoreDependent (std::weak_ptr <Graphics::Core> const& core) :
            myCore (core)
        {}

    public:
        CoreDependent () = delete;

        [[nodiscard]] std::weak_ptr <Graphics::Core>
        get_core() const noexcept {
            return myCore;
        }

    protected:
        void assert_creation_thread () const noexcept {
            myCore->assert_creation_thread();
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

    private:
        std::shared_ptr <Graphics::Core> myCore;
    };
}

#endif