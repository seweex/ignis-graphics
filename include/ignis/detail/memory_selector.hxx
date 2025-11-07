#ifndef IGNIS_DETAIL_MEMORY_SELECTOR_HXX
#define IGNIS_DETAIL_MEMORY_SELECTOR_HXX

#include <optional>
#include <functional>

#include <boost/container/small_vector.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/container/flat_map.hpp>

#include <vulkan/vulkan_raii.hpp>

namespace Ignis::Detail
{
    struct MemoryHeapProperties
    {
        uint32_t index;
        uint32_t flagsScore;
        uint32_t maxFlagsScore;

        size_t totalSize;
        size_t bestAvailableSize;
        size_t worstAvailableSize;
        size_t usersNumber;
    };
}

template <>
struct std::greater <Ignis::Detail::MemoryHeapProperties>
{
    [[nodiscard]] constexpr bool operator () (
        Ignis::Detail::MemoryHeapProperties const& left,
        Ignis::Detail::MemoryHeapProperties const& right) const noexcept
    {
        std::greater <> constexpr underlying;

        return underlying (
            left.flagsScore * left.bestAvailableSize,
            right.flagsScore * right.bestAvailableSize);
    }
};

namespace Ignis::Graphics
{
    enum class PreferMemory
        { fast_access, large_storage };
}

namespace Ignis::Detail
{
    enum class MemoryType
        { immutable, mappable };

    template <MemoryType Type>
    struct OptimalMemoryFlagsWeights;

    template <>
    struct OptimalMemoryFlagsWeights <MemoryType::immutable>
    {
        static constexpr size_t device_local = 32;
        static constexpr size_t host_coherent = 1;
    };

    template <>
    struct OptimalMemoryFlagsWeights <MemoryType::mappable>
    {
        static constexpr size_t device_local = 16;
        static constexpr size_t host_coherent = 8;
    };

    template <MemoryType Type>
    class OptimalMemorySelector final
    {
        static constexpr size_t memory_types_hint = 12;

        using HeapSet = boost::container::small_flat_multiset
            <MemoryHeapProperties, memory_types_hint, std::greater <MemoryHeapProperties>>;

        /* fill the registry */

        [[nodiscard]] static bool
        is_mappable (vk::MemoryPropertyFlags const flags) noexcept {
            return static_cast <bool> (flags & vk::MemoryPropertyFlagBits::eHostVisible);
        }

        [[nodiscard]] static constexpr size_t
        get_max_flags_weight (Graphics::PreferMemory const preference) noexcept
        {
            std::array const possibleWights = {
                get_flags_weight ({}, preference),
                get_flags_weight (vk::MemoryPropertyFlagBits::eDeviceLocal, preference),
                get_flags_weight (vk::MemoryPropertyFlagBits::eHostCoherent, preference),
                get_flags_weight (
                    vk::MemoryPropertyFlagBits::eHostCoherent |
                    vk::MemoryPropertyFlagBits::eDeviceLocal, preference),
            };

            return std::ranges::max (possibleWights, std::less{});
        }

        [[nodiscard]] static constexpr size_t
        get_flags_weight (
            vk::MemoryPropertyFlags const flags,
            Graphics::PreferMemory const preference) noexcept
        {
            constexpr size_t preferred_multiplier = 8;
            size_t weight = 16;

            if (flags & vk::MemoryPropertyFlagBits::eHostCoherent)
            {
                if (preference == Graphics::PreferMemory::fast_access)
                    weight *= OptimalMemoryFlagsWeights <Type>::host_coherent / preferred_multiplier;
                else
                    weight *= OptimalMemoryFlagsWeights <Type>::host_coherent;
            }

            if (flags & vk::MemoryPropertyFlagBits::eDeviceLocal)
            {
                if (preference == Graphics::PreferMemory::fast_access)
                    weight *= preferred_multiplier * OptimalMemoryFlagsWeights <Type>::device_local;
                else
                    weight *= OptimalMemoryFlagsWeights <Type>::device_local;
            }
            else if (preference == Graphics::PreferMemory::fast_access)
                weight /= preferred_multiplier;

            return std::max <size_t> (weight, 1);
        }

        [[nodiscard]] static size_t
        rate_memory_amount (size_t const bytes) noexcept
        {
            constexpr size_t block_size = 1 * 1024 * 1024; /* 1 MB */
            constexpr size_t scale = 16;

            auto const blocks = bytes / block_size;
            auto const score = std::sqrt (blocks);

            return scale * static_cast <size_t> (std::round (score));
        }

        [[nodiscard]] static HeapSet
        make_heap_set (
            vk::raii::PhysicalDevice const& device,
            Graphics::PreferMemory const preference)
        {
            auto const properties = device.getMemoryProperties();

            HeapSet heaps;
            heaps.reserve (properties.memoryTypeCount);

            for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            {
                auto const [typeFlags, index] = properties.memoryTypes[i];
                auto const size = properties.memoryHeaps[index].size;

                auto const flagsScore = get_flags_weight (typeFlags, preference);
                auto const maxFlagsScore = get_max_flags_weight (preference);

                if constexpr (Type == MemoryType::mappable)
                    if (!is_mappable (typeFlags)) [[unlikely]]
                        continue;

                /* index, flagsScore, maxFlagsScore, totalSize, bestAvailableSize, worstAvailableSize, usersNumber */
                heaps.emplace (i, flagsScore, maxFlagsScore, size, size, size, 0);
            }

            if (heaps.empty()) [[unlikely]]
                throw std::runtime_error ("No acceptable memory was found");

            return heaps;
        }

        /* use the registry */

        [[nodiscard]] static float
        estimate_suitability (
            MemoryHeapProperties const& heap,
            size_t const size,
            uint32_t const indices) noexcept
        {
            if (size > heap.bestAvailableSize ||
                0 == (1 << heap.index & indices)) [[unlikely]]
                return 0;

            auto const availableSizeSpread = heap.bestAvailableSize - heap.worstAvailableSize;

            if (availableSizeSpread == 0) [[unlikely]]
                return 1;

            auto const flagsFactor =
                static_cast <float> (heap.flagsScore) /
                static_cast <float> (heap.maxFlagsScore);

            auto const fitRatio =
                static_cast <float> (heap.bestAvailableSize - size) /
                static_cast <float> (availableSizeSpread);

            auto const fitFactor = std::min (fitRatio * fitRatio, 1.f);

            auto const usedSize = heap.totalSize - heap.bestAvailableSize;
            auto const usersRatio =
                static_cast <float> (usedSize) /
                static_cast <float> (heap.totalSize);

            auto const usersFactor = 1.f - usersRatio * usersRatio;

            constexpr float flags_weight = 0.3;
            constexpr float fit_weight = 0.4;
            constexpr float users_weight = 0.3;

            return flags_weight * flagsFactor +
                fitFactor * fit_weight +
                usersFactor * users_weight;
        }

        [[nodiscard]] static MemoryHeapProperties
        make_properties_after_emplace (
            MemoryHeapProperties const& before,
            size_t const size) noexcept
        {
            return {
                before.index,
                before.flagsScore,
                before.maxFlagsScore,
                before.totalSize,
                before.bestAvailableSize - size,
                std::max <size_t> ((before.worstAvailableSize - size) / 2, 0),
                before.usersNumber + 1
            };
        }

        [[nodiscard]] std::optional <HeapSet::const_iterator> find_suitable_memory (
            HeapSet const& heaps,
            vk::MemoryRequirements const& requirements) const
        {
            auto const& sequence = heaps.sequence();

            auto const firstNonSuitable = std::ranges::upper_bound (sequence, requirements.size, std::greater{},
                [] (auto const& heap) { return heap.bestAvailableSize; });

            boost::container::small_flat_multimap
                <float, HeapSet::const_iterator, memory_types_hint> suitableHeaps;

            suitableHeaps.reserve (sequence.size());

            for (auto iter = sequence.begin(); iter != firstNonSuitable; ++iter)
                if (auto const suitability = estimate_suitability (
                        *iter, requirements.size, requirements.memoryTypeBits);
                    suitability > 0) [[likely]]
                {
                    suitableHeaps.emplace (suitability, iter);
                }

            if (suitableHeaps.empty()) [[unlikely]]
                return std::nullopt;
            else
                return suitableHeaps.begin()->second;
        }

        [[nodiscard]] std::pair <HeapSet&, HeapSet::const_iterator>
        choose_memory_heap (
            vk::MemoryRequirements const& requirements,
            Graphics::PreferMemory const preference)
        {
            auto [preferredHeaps, fallbackHeaps] =
                preference == Graphics::PreferMemory::fast_access ?
                    std::forward_as_tuple (myFastHeaps, myLargeHeaps) :
                    std::forward_as_tuple (myLargeHeaps, myFastHeaps);

            if (auto suitable = find_suitable_memory (preferredHeaps, requirements)) [[likely]]
                return { preferredHeaps, *suitable };

            else if ((suitable = find_suitable_memory (fallbackHeaps, requirements))) [[likely]]
                return { fallbackHeaps, *suitable };

            throw std::runtime_error("No suitable heaps was found");
        }

    public:
        explicit OptimalMemorySelector (vk::raii::PhysicalDevice const& device) :
            myFastHeaps  (make_heap_set (device, Graphics::PreferMemory::fast_access)),
            myLargeHeaps (make_heap_set (device, Graphics::PreferMemory::large_storage))
        {}

        [[nodiscard]] uint32_t request_memory_heap (
            vk::MemoryRequirements const& requirements,
            Graphics::PreferMemory const preference)
        {
            auto [heaps, iter] = choose_memory_heap (requirements, preference);

            auto const oldHeap = *iter;
            auto const newHeap = make_properties_after_emplace (oldHeap, requirements.size);

            iter = heaps.erase (iter);
            heaps.emplace_hint (iter, newHeap);

            return oldHeap.index;
        }

    private:
        HeapSet myFastHeaps;
        HeapSet myLargeHeaps;
    };
}

#endif