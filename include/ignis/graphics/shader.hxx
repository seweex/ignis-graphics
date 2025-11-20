#ifndef IGNIS_GRAPHICS_SHADER_HXX
#define IGNIS_GRAPHICS_SHADER_HXX

#include <cassert>
#include <memory>
#include <mutex>

#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <vulkan/vulkan_raii.hpp>
#include <shaderc/shaderc.hpp>

#include <ignis/detail/debug_assert.hxx>
#include <ignis/detail/vulkan_functional.hxx>
#include <ignis/detail/core_dependent.hxx>

#include "ignis/detail/enable_sync.hxx"

namespace Ignis::Detail
{
    template <class Ty>
    concept ShaderBinaryCode =
        std::ranges::contiguous_range <Ty> &&
        std::ranges::sized_range <Ty> &&
        std::same_as <std::ranges::range_value_t <Ty>, uint32_t>;
}

namespace Ignis::Graphics
{
    template <bool InternalSync>
    class ShaderFactory;

    enum class ShaderType
    {
        vertex,
        fragment,
        geometry,
        compute,
        tesselation_control,
        tesselation_evaluation,

#if !NDEBUG
        first_enum_value = vertex,
        last_enum_value = tesselation_evaluation
#endif
    };

    enum class CompileOptimization
    {
        size,
        performance,

#if !NDEBUG
        first_enum_value = size,
        last_enum_value = performance
#endif
    };

    template <ShaderType Type>
    class ShaderBinary final
    {
        template <class BinaryTy>
        ShaderBinary (BinaryTy&& binary, [[maybe_unused]] std::true_type const isVector) noexcept :
            myCode (std::move (binary))
        {}

        template <class BinaryTy>
        ShaderBinary (BinaryTy&& binary, [[maybe_unused]] std::false_type const isVector) :
            myCode (binary.begin(), binary.end())
        {}

        template <bool InternalSync>
        friend class ShaderFactory;

    public:
        template <Detail::ShaderBinaryCode BinaryTy>
            requires (!std::same_as <std::remove_cvref_t <BinaryTy>, ShaderBinary>)
        explicit ShaderBinary (BinaryTy&& binary)
            noexcept (std::is_same_v <std::remove_cvref_t <BinaryTy>, std::vector <uint32_t>>)
        :
            ShaderBinary (std::forward <BinaryTy> (binary), std::is_same <BinaryTy, std::vector <uint32_t>>{})
        {}

        ShaderBinary (ShaderBinary&&) noexcept = default;
        ShaderBinary (ShaderBinary const&) noexcept = default;

        ShaderBinary& operator=(ShaderBinary&&) = default;
        ShaderBinary& operator=(ShaderBinary const&) = default;

        [[nodiscard]] bool
        empty () const noexcept {
            return myCode.empty();
        }

        void clear () noexcept {
            return myCode.clear();
        }

    private:
        std::vector <uint32_t> myCode;
    };

    template <ShaderType Type>
    class Shader final
    {
        explicit Shader (vk::ShaderModule const shader) noexcept :
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

    class ShaderCompiler final :
        public Detail::VulkanApiDependent
    {
        template <ShaderType Type>
        [[nodiscard]] static consteval shaderc_shader_kind get_shader_kind () noexcept
        {
            switch (Type)
            {
            case ShaderType::vertex:
                return shaderc_vertex_shader;

            case ShaderType::compute:
                return shaderc_compute_shader;

            case ShaderType::fragment:
                return shaderc_fragment_shader;

            case ShaderType::geometry:
                return shaderc_geometry_shader;

            case ShaderType::tesselation_control:
                return shaderc_tess_control_shader;

            case ShaderType::tesselation_evaluation:
                return shaderc_tess_evaluation_shader;
            }
        }

    public:
        explicit ShaderCompiler (std::weak_ptr <Core> const& core) :
            CoreDependent (core)
        {
            myOptions.SetTargetEnvironment (
                shaderc_target_env_vulkan, VulkanApiDependent::get_vulkan_version());
        }

        ShaderCompiler (ShaderCompiler &&) noexcept = default;
        ShaderCompiler (ShaderCompiler const&) = delete;

        ShaderCompiler& operator=(ShaderCompiler &&) = delete;
        ShaderCompiler& operator=(ShaderCompiler const&) = delete;

        [[nodiscard]] ShaderCompiler&
        push_macro (std::string_view const name, std::string_view const value = {})
        {
            assert (!name.empty());

            myOptions.AddMacroDefinition (name.data(), name.size(), value.data(), value.size());
            return *this;
        }

        [[nodiscard]] ShaderCompiler&
        enable_debug () {
            myOptions.SetGenerateDebugInfo();
            return *this;
        }

        [[nodiscard]] ShaderCompiler&
        optimize (CompileOptimization const optimization)
        {
            assert (Detail::is_enum_valid (optimization));

            switch (optimization)
            {
            case CompileOptimization::performance:
                myOptions.SetOptimizationLevel (shaderc_optimization_level_performance);
                break;

            case CompileOptimization::size:
                myOptions.SetOptimizationLevel (shaderc_optimization_level_size);
                break;
            }

            return *this;
        }

        template <ShaderType Type>
            requires (Detail::is_enum_valid (Type))
        [[nodiscard]] ShaderBinary <Type> compile (
            std::string_view const source,
            std::string_view const name,
            std::string_view const entry = "main")
        {
            assert (!source.empty());
            assert (!name.empty());
            assert (!entry.empty());

            auto constexpr kind = get_shader_kind <Type> ();
            auto const result = myCompiler.CompileGlslToSpv (
                source.data(), source.size(), kind, name.data(), entry.data(), myOptions);

            if (result.GetCompilationStatus() != shaderc_compilation_status_success) [[unlikely]]
                throw std::runtime_error { result.GetErrorMessage() };

            return ShaderBinary <Type> { result };
        }

    private:
        shaderc::CompileOptions myOptions;
        shaderc::Compiler myCompiler;
    };

    template <bool InternalSync>
    class ShaderFactory final :
        public Detail::DeviceDependent
    {
        [[nodiscard]] vk::raii::ShaderModule
        create_shader (size_t const size, void const* const data) const
        {
            vk::ShaderModuleCreateInfo const createInfo
                { {}, size, static_cast <uint32_t const*> (data) };

            return { DeviceDependent::get_device(), createInfo };
        }

    public:
        explicit ShaderFactory (std::weak_ptr <Core> const& core) :
            CoreDependent (core)
        {}

        ShaderFactory (ShaderFactory &&) = delete;
        ShaderFactory (ShaderFactory const&) = delete;

        ShaderFactory& operator=(ShaderFactory &&) = delete;
        ShaderFactory& operator=(ShaderFactory const&) = delete;

        template <ShaderType Type>
        [[nodiscard]] Shader <Type> make_shader (ShaderBinary <Type> const& binary)
        {
            assert (!binary.myCode.empty());

            auto shader = create_shader (
                binary.myCode.size() * sizeof(uint32_t), binary.myCode.data());

            [[maybe_unused]] auto const lock =
                Detail::lock_mutex <InternalSync, std::lock_guard> (myMutex);

            [[maybe_unused]] auto const [iter, inserted] =
                myShaders.emplace (std::move (shader));

            return Shader { iter->first };
        }

        template <ShaderType Type>
        void destroy_shader (Shader <Type> const shader) noexcept (!InternalSync)
        {
            [[maybe_unused]] auto const lock =
                Detail::lock_mutex <InternalSync, std::lock_guard> (myMutex);

            myShaders.erase (shader.myShader);
        }

    private:
        [[no_unique_address]] Detail::EnableMutex <InternalSync, std::mutex> myMutex;

        boost::unordered::unordered_flat_map
            <vk::raii::ShaderModule, Detail::VulkanHash, Detail::VulkanEquals>
        myShaders;
    };
}

#endif