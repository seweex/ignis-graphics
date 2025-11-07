#ifndef IGNIS_DETAIL_RESOURCE_ALLOCATOR_HXX
#define IGNIS_DETAIL_RESOURCE_ALLOCATOR_HXX

#include <cmath>

#include <boost/container/flat_map.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>
#include <ignis/detail/memory_selector.hxx>

namespace Ignis::Detail
{
    template <bool InternalSync>
    class ResourceAllocator :
        public virtual CoreDependent
    {
        template <MemoryType Type>
        [[nodiscard]] uint32_t get_memory_index (
            vk::MemoryRequirements const& requirements,
            Graphics::PreferMemory const preference)
        {
            auto [mutex, selector] = std::invoke([&] () mutable
            {
                if constexpr (Type == MemoryType::immutable)
                    return std::forward_as_tuple (myImmutableSelectorMutex, myImmutableSelector);
                else
                    return std::forward_as_tuple (myMappableSelectorMutex, myMappableSelector);
            });

            [[maybe_unused]] auto const lock =
                lock_mutex <InternalSync, std::lock_guard> (mutex);

            return selector.request_memory_heap (requirements, preference);
        }

    protected:
        ResourceAllocator () :
            myImmutableSelector (CoreDependent::get_physical_device()),
            myMappableSelector  (CoreDependent::get_physical_device())
        {}

        template <MemoryType Type>
        [[nodiscard]] vk::raii::DeviceMemory allocate (
            vk::MemoryRequirements const requirements,
            Graphics::PreferMemory const preference)
        {
            auto const& device = CoreDependent::get_device();
            auto const memory = get_memory_index <Type> (requirements, preference);

            vk::MemoryAllocateInfo const allocateInfo
                { requirements.size, memory };

            return { device, allocateInfo };
        }

    private:
        [[no_unique_address]] EnableMutex <InternalSync, std::mutex> mutable myImmutableSelectorMutex;
        OptimalMemorySelector <MemoryType::immutable> myImmutableSelector;

        [[no_unique_address]] EnableMutex <InternalSync, std::mutex> mutable myMappableSelectorMutex;
        OptimalMemorySelector <MemoryType::mappable> myMappableSelector;
    };
}

#endif