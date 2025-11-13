#ifndef IGNIS_GRAPHICS_CORE_HXX
#define IGNIS_GRAPHICS_CORE_HXX

#include <optional>
#include <ranges>
#include <array>
#include <string_view>
#include <utility>
#include <functional>

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/container/small_vector.hpp>

#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/glfw_dependent.hxx>
#include <ignis/detail/debug_assert.hxx>

#include <ignis/graphics/window.hxx>

namespace Ignis::Detail
{
    class CoreDependent;

    enum class FamilyType
    {
        graphics,
        transfer,
        present
    };

    struct QueueIndices
    {
        uint32_t graphics;
        uint32_t transfer;
        uint32_t present;
    };

    struct FamilyAndQueueIndices
    {
        QueueIndices families;
        QueueIndices queues;
    };
}

namespace Ignis
{
    struct SoftwareInfo
    {
        std::string_view name;
        uint32_t versionMajor;
        uint32_t versionMinor;
        uint32_t versionPatch;
    };
}

namespace Ignis::Graphics
{
    class Window;

    class Core final :
        private Detail::CreationThreadAsserter,
        private Detail::GLFWDependent
    {
        static constexpr std::array required_extensions =
            { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        [[nodiscard]] vk::raii::Instance
        make_instance (SoftwareInfo const& application, SoftwareInfo const& engine) const
        {
            assert (!application.name.empty());
            assert (!engine.name.empty());

            auto const version = myContext.enumerateInstanceVersion();

            vk::ApplicationInfo const applicationInfo
            {
                application.name.data(),
                VK_MAKE_VERSION
                    (application.versionMajor, application.versionMinor, application.versionPatch),

                engine.name.data(),
                VK_MAKE_VERSION
                    (engine.versionMajor, engine.versionMinor, engine.versionPatch),

                version
            };

            auto const extensions = GLFWDependent::get_extensions();
            auto const layers = GLFWDependent::get_layers();

            vk::InstanceCreateInfo const createInfo {
                {},
                &applicationInfo,
                static_cast<uint32_t>(layers.size()),
                layers.data(),
                static_cast<uint32_t>(extensions.size()),
                extensions.data()
            };

            return { myContext, createInfo };
        }

        [[nodiscard]] std::pair <vk::raii::PhysicalDevice, Detail::FamilyAndQueueIndices>
        pick_physical_device ()
        {
            vk::raii::PhysicalDevices deviceList { myInstance };

            boost::container::small_flat_map <size_t, Detail::FamilyAndQueueIndices, 5, std::greater <>>
            indicesList;

            boost::container::small_flat_multimap <size_t, size_t, 5, std::greater <>>
            ratedDevices;

            indicesList.reserve (deviceList.size());
            ratedDevices.reserve (deviceList.size());

            bool hasAtLeastOne = false;

            auto supportsSurface =
                [this] (vk::raii::PhysicalDevice const& device)
                {
                    return !device.getSurfaceFormatsKHR (mySurface).empty() &&
                        !device.getSurfacePresentModesKHR (mySurface).empty();
                };

            auto supportsExtensions =
                [] (vk::raii::PhysicalDevice const& device)
                {
                    boost::container::small_flat_map
                        <std::string_view, bool, required_extensions.size()>
                    supports;

                    for (auto const extension : required_extensions)
                        supports.emplace (extension, false);

                    for (auto const properties : device.enumerateDeviceExtensionProperties())
                        if (auto const iter = supports.find (properties.extensionName);
                            iter != supports.end())
                            iter->second = true;

                    return std::ranges::all_of (supports,
                        [] (auto const& properties) { return properties.second; });
                };

            auto supportsFeatures =
                [] (vk::raii::PhysicalDevice const& device)
                {
                    auto const features = device.getFeatures();

                    return features.sampleRateShading &&
                        features.samplerAnisotropy;
                };

            auto rateType =
                [] (vk::raii::PhysicalDevice const& device) -> size_t
                {
                    switch (device.getProperties().deviceType)
                    {
                    case vk::PhysicalDeviceType::eDiscreteGpu:
                        return 100'000;

                    case vk::PhysicalDeviceType::eVirtualGpu:
                        return 30'000;

                    case vk::PhysicalDeviceType::eIntegratedGpu:
                        return 10'000;

                    case vk::PhysicalDeviceType::eCpu:
                        return 5'000;

                    default:
                        return 1'000;
                    }
                };

            auto rateMemory =
                [] (vk::raii::PhysicalDevice const& device) -> size_t
                {
                    auto const memoryProperties = device.getMemoryProperties();
                    size_t score = 0;

                    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
                    {
                        size_t constexpr rating_block_size = 1024 * 1024;
                        size_t constexpr local_memory_weight = 16;
                        size_t constexpr visible_memory_weight = 4;

                        auto const index = memoryProperties.memoryTypes[i].heapIndex;
                        auto const flags = memoryProperties.memoryTypes[i].propertyFlags;

                        size_t memory = memoryProperties.memoryHeaps[index].size;

                        if (flags & vk::MemoryPropertyFlagBits::eDeviceLocal)
                            memory *= local_memory_weight;

                        if (flags & vk::MemoryPropertyFlagBits::eHostVisible)
                            memory *= visible_memory_weight;

                        auto const blocks = memory / rating_block_size;
                        score = std::max (score, blocks);
                    }

                    return score;
                };

            auto pickQueues =
                [this] (vk::raii::PhysicalDevice const& device)
                    -> std::optional <Detail::FamilyAndQueueIndices>
                {
                    using FamilyIndices = boost::container::small_vector <uint32_t, 8>;
                    using FamilyTypes = boost::container::small_flat_map <Detail::FamilyType, FamilyIndices, 3>;

                    FamilyTypes typesFamilies = {
                        { Detail::FamilyType::graphics, {} },
                        { Detail::FamilyType::transfer, {} },
                        { Detail::FamilyType::present, {} }
                    };

                    auto const familyProperties = device.getQueueFamilyProperties();

                    for (uint32_t i = 0; i < familyProperties.size(); ++i)
                    {
                        auto const flags = familyProperties[i].queueFlags;

                        if (flags & vk::QueueFlagBits::eGraphics)
                            typesFamilies [Detail::FamilyType::graphics].emplace_back(i);

                        if (flags & vk::QueueFlagBits::eTransfer)
                            typesFamilies [Detail::FamilyType::transfer].emplace_back(i);

                        if (device.getSurfaceSupportKHR (i, mySurface))
                            typesFamilies [Detail::FamilyType::present].emplace_back(i);
                    }

                    if (typesFamilies [Detail::FamilyType::graphics].empty() ||
                        typesFamilies [Detail::FamilyType::transfer].empty() ||
                        typesFamilies [Detail::FamilyType::present].empty()) [[unlikely]]
                        return std::nullopt;

                    boost::container::small_flat_map
                        <size_t, Detail::FamilyAndQueueIndices, 3, std::greater <>>
                    separationToIndices;

                    for (auto const graphicsFamily : typesFamilies [Detail::FamilyType::graphics])
                    for (auto const transferFamily : typesFamilies [Detail::FamilyType::transfer])
                    for (auto const presentFamily : typesFamilies [Detail::FamilyType::present])
                    {
                        boost::container::small_flat_map <uint32_t, uint32_t, 3> familyToCount;

                        auto const graphicsQueue = familyToCount [graphicsFamily] ++;
                        auto const transferQueue = familyToCount [transferFamily] ++;
                        auto const presentQueue = familyToCount [presentFamily] ++;

                        uint32_t const separationFactor = familyToCount.size();

                        if (separationToIndices.contains (separationFactor) &&
                            std::ranges::all_of (familyToCount, [&] (auto const& pair)
                                { return pair.second > familyProperties[pair.first].queueCount; })) [[likely]]
                            continue;

                        separationToIndices.emplace (std::piecewise_construct,
                            std::forward_as_tuple (separationFactor),
                            std::forward_as_tuple (
                                Detail::QueueIndices{ graphicsFamily, transferFamily, presentFamily },
                                Detail::QueueIndices{ graphicsQueue, transferQueue, presentQueue }));

                        break;
                    }

                    for (auto const indices : separationToIndices | std::views::values)
                        return indices;

                    return std::nullopt;
                };

            for (size_t i = 0; i < deviceList.size(); ++i)
            {
                auto const& device = deviceList[i];

                if (!supportsSurface (device) ||
                    !supportsExtensions (device) ||
                    !supportsFeatures (device)) [[unlikely]]
                    continue;

                auto const families = pickQueues (device);

                if (!families) [[unlikely]]
                    continue;

                auto const score = rateType(device) + rateMemory(device);

                indicesList.emplace (i, *families);
                ratedDevices.emplace (score, i);

                hasAtLeastOne = true;
            }

            if (!hasAtLeastOne) [[unlikely]]
                throw std::runtime_error("No suitable device found");

            auto const bestIndex = ratedDevices.begin()->second;

            return std::make_pair (std::move (deviceList[bestIndex]), indicesList [bestIndex]);
        }

        [[nodiscard]] vk::raii::Device
        make_device () const
        {
            boost::container::small_flat_map <uint32_t, uint32_t, 3> familyToCount;

            ++ familyToCount [myIndices.families.graphics];
            ++ familyToCount [myIndices.families.transfer];
            ++ familyToCount [myIndices.families.present];

            boost::container::small_vector <vk::DeviceQueueCreateInfo, 3> queueInfo;
            std::array constexpr priorities = { 1.f, 1.f, 1.f };

            for (auto const [family, count] : familyToCount)
                queueInfo.emplace_back (vk::DeviceQueueCreateFlags{}, family, count, priorities.data());

            vk::PhysicalDeviceFeatures requiredFeatures;
            requiredFeatures.samplerAnisotropy = true;
            requiredFeatures.sampleRateShading = true;

            vk::DeviceCreateInfo const createInfo {
                {},
                static_cast<uint32_t>(queueInfo.size()),
                queueInfo.data(),
                0,
                nullptr,
                static_cast<uint32_t>(required_extensions.size()),
                required_extensions.data(),
                &requiredFeatures
            };

            return { myPhysicalDevice, createInfo };
        }

    public:
        Core (Window& window, SoftwareInfo const& application, SoftwareInfo const& engine)
        :
            myInstance (make_instance (application, engine)),
            mySurface  (window.create_surface (myInstance)),
            myPhysicalDevice (VK_NULL_HANDLE),
            myDevice         (VK_NULL_HANDLE)
        {
            auto [device, indices] = pick_physical_device ();

            myPhysicalDevice = std::move (device);
            myIndices = indices;

            myDevice = make_device();
        }

        [[nodiscard]] uint32_t
        get_vulkan_version() const noexcept {
            return myContext.enumerateInstanceVersion();
        }

    private:
        vk::raii::Context myContext;
        vk::raii::Instance myInstance;

        vk::raii::SurfaceKHR mySurface;

        vk::raii::PhysicalDevice myPhysicalDevice;
        Detail::FamilyAndQueueIndices myIndices;

        vk::raii::Device myDevice;

        using CreationThreadAsserter::assert_creation_thread;
        friend class Detail::CoreDependent;
    };
}

#endif