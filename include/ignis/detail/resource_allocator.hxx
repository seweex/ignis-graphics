#ifndef IGNIS_DETAIL_RESOURCE_ALLOCATOR_HXX
#define IGNIS_DETAIL_RESOURCE_ALLOCATOR_HXX

#include <cmath>

#include <boost/container/flat_map.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/core_dependent.hxx>

namespace Ignis::Detail
{
    enum class MemoryType
        { immutable, mappable };

    struct MemoryIndices
    {
        uint32_t immutable;
        uint32_t mappable;
    };

    template <MemoryType Type>
    struct OptimalMemoryFlagsWeights;

    template <>
    struct OptimalMemoryFlagsWeights <MemoryType::immutable>
    {
        static constexpr size_t device_local = 25;
        static constexpr size_t host_coherent = 0;
    };

    template <>
    struct OptimalMemoryFlagsWeights <MemoryType::mappable>
    {
        static constexpr size_t device_local = 25;
        static constexpr size_t host_coherent = 10;
    };

    class ResourceAllocator :
        public virtual CoreDependent
    {
        [[nodiscard]] static bool
        is_mappable (vk::MemoryPropertyFlags const flags) noexcept {
            return static_cast <bool> (flags & vk::MemoryPropertyFlagBits::eHostVisible);
        }

        template <MemoryType Type>
        [[nodiscard]] static size_t retrieve_flags_weight (
            vk::MemoryPropertyFlags const flags) noexcept
        {
            size_t weight = 0;

            if (flags & vk::MemoryPropertyFlagBits::eDeviceLocal)
                weight += OptimalMemoryFlagsWeights <Type>::device_local;

            if (flags & vk::MemoryPropertyFlagBits::eHostCoherent)
                weight += OptimalMemoryFlagsWeights <Type>::host_coherent;

            return weight;
        }

        [[nodiscard]] static size_t rate_memory_amount (size_t const bytes) noexcept
        {
            static constexpr size_t block_size = 1 * 1024 * 1024; /* 1 MB */
            static constexpr size_t scale = 100;

            auto const blocks = bytes / block_size;
            auto const score = std::sqrt (static_cast <double> (blocks));

            return scale * static_cast <size_t> (std::round (score));
        }

        template <MemoryType Type>
        [[nodiscard]] static uint32_t pick_memory_type (
            vk::PhysicalDeviceMemoryProperties const& properties)
        {
            static constexpr size_t memory_types_hint = 10;

            boost::container::small_flat_map <size_t, uint32_t, memory_types_hint, std::greater <>>
            ratedTypes;

            ratedTypes.reserve (properties.memoryTypeCount);

            for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            {
                auto const [flags, index] = properties.memoryTypes[i];

                auto const weight = retrieve_flags_weight <Type> (flags);
                auto const score = rate_memory_amount (properties.memoryHeaps[index].size);

                if constexpr (Type == MemoryType::mappable)
                    if (!is_mappable(flags)) [[unlikely]]
                        continue;

                ratedTypes.emplace (score, i);
            }

            if (ratedTypes.empty()) [[unlikely]]
                throw std::runtime_error ("No acceptable memory was found");

            return ratedTypes.begin()->second;
        }

        [[nodiscard]] MemoryIndices pick_memory_indices () const
        {
            auto const& device = CoreDependent::get_physical_device();
            auto const properties = device.getMemoryProperties();

            return {
                pick_memory_type <MemoryType::immutable> (properties),
                pick_memory_type <MemoryType::mappable> (properties)
            };
        }

        template <MemoryType Type>
        [[nodiscard]] uint32_t get_memory_index () const noexcept
        {
            if constexpr (Type == MemoryType::immutable)
                return myMemoryIndices.immutable;

            else if constexpr (Type == MemoryType::mappable)
                return myMemoryIndices.mappable;

            else
                static_assert (false, "Invalid Type");
        }

    protected:
        ResourceAllocator () :
            myMemoryIndices (pick_memory_indices ())
        {}

        template <MemoryType Type>
        [[nodiscard]] vk::raii::DeviceMemory allocate (size_t const size)
        {
            auto const& device = CoreDependent::get_device();
            auto const memory = get_memory_index <Type> ();

            vk::MemoryAllocateInfo const info { size, memory };

            return { device, info };
        }

    private:
        MemoryIndices myMemoryIndices;
    };
}

#endif