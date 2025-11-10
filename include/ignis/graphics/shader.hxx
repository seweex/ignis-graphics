#ifndef IGNIS_GRAPHICS_SHADER_HXX
#define IGNIS_GRAPHICS_SHADER_HXX

#include <cassert>
#include <memory>
#include <mutex>

#include <boost/unordered/unordered_flat_set.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/vulkan_functional.hxx>
#include <ignis/detail/core_dependent.hxx>

namespace Ignis::Graphics
{
    class Shader final
    {
        Shader (vk::ShaderModule const shader) noexcept :
            myShader (shader)
        {}

    public:
        Shader (Shader const&) noexcept = default;
        Shader& operator=(Shader const&) noexcept = default;

        Shader (Shader&& other) noexcept :
            myShader (std::exchange (other.myShader, VK_NULL_HANDLE))
        {}

        Shader& operator=(Shader&& other) noexcept
        {
            if (this == &other) [[unlikely]]
                return *this;

            myShader = std::exchange (other.myShader, VK_NULL_HANDLE);
            return *this;
        }

        template <bool InternalSync>
        friend class ShaderFactory;

        [[nodiscard]] bool
        is_valid () const noexcept {
            return myShader;
        }

    private:
        vk::ShaderModule myShader;
    };

    template <bool InternalSync>
    class ShaderFactory :
        public virtual Detail::CoreDependent
    {
        [[nodiscard]] vk::raii::ShaderModule
        create_shader (size_t const size, void const* const data) const
        {
            assert (size > 0 && size % sizeof(uint32_t) == 0);
            assert (data);

            vk::ShaderModuleCreateInfo const createInfo
                { {}, size, static_cast <uint32_t const*> (data) };

            return { CoreDependent::get_device(), createInfo };
        }

    protected:
        ShaderFactory () noexcept = default;

    public:
        explicit ShaderFactory (std::weak_ptr <Core> const& core) :
            CoreDependent (core)
        {}

        [[nodiscard]] Shader make_shader (size_t const size, void const* const binary)
        {
            auto shader = create_shader (size, binary);

            [[maybe_unused]] auto const lock =
                Detail::lock_mutex <InternalSync, std::lock_guard> (myMutex);

            [[maybe_unused]] auto const [iter, inserted] =
                myShaders.emplace (std::move (shader));

            return Shader { iter->first };
        }

    private:
        [[no_unique_address]] Detail::EnableMutex <InternalSync, std::mutex> myMutex;

        boost::unordered::unordered_flat_map
            <vk::raii::ShaderModule, Detail::VulkanHash, Detail::VulkanEquals>
        myShaders;
    };

    template <bool InternalSync>
    class ShaderManager :
        public virtual Detail::CoreDependent,
        public virtual ShaderFactory <InternalSync>
    {
    public:
        explicit ShaderManager (std::weak_ptr <Core> const& core) :
            CoreDependent (core)
        {}
    };
}

#endif