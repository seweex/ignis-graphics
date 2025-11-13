#ifndef IGNIS_GRAPHICS_RENDER_PASS_HXX
#define IGNIS_GRAPHICS_RENDER_PASS_HXX

#include <optional>
#include <memory>

#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/container/flat_set.hpp>

#include <vulkan/vulkan_raii.hpp>

#include <ignis/detail/transparent_hash.hxx>
#include <ignis/detail/vulkan_functional.hxx>
#include <ignis/detail/debug_assert.hxx>

#include <ignis/detail/core_dependent.hxx>

namespace Ignis::Detail
{
    struct SubpassInfo
    {
        uint32_t index;

        std::vector <vk::AttachmentReference> color;
        std::vector <vk::AttachmentReference> input;

        std::optional <vk::AttachmentReference> depth;
        std::optional <vk::AttachmentReference> msaa;
    };
}

namespace Ignis::Graphics
{
    enum class DataFormat : std::underlying_type_t <vk::Format>
    {
        x_float  = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32Sfloat),
        x_double = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR64Sfloat),
        x_int    = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32Sint),
        x_uint   = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32Uint),

        xy_float  = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32G32Sfloat),
        xy_double = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR64G64Sfloat),
        xy_int    = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32G32Sint),
        xy_uint   = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32G32Uint),

        xyz_float  = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32G32B32Sfloat),
        xyz_double = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR64G64B64Sfloat),
        xyz_int    = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32G32B32Sint),
        xyz_uint   = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32G32B32Uint),

        xyzw_float  = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32G32B32A32Sfloat),
        xyzw_double = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR64G64B64A64Sfloat),
        xyzw_int    = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32G32B32A32Sint),
        xyzw_uint   = static_cast <std::underlying_type_t <vk::Format>> (vk::Format::eR32G32B32A32Uint),

#if !NDEBUG
        first_enum_value = x_float,
        last_enum_value = xyzw_uint
#endif
    };

    enum class InputAttachmentFormat
    {
        color,
        depth,

#if !NDEBUG
        first_enum_value = color,
        last_enum_value = depth
#endif
    };

    class RenderPass final
    {
        explicit RenderPass (vk::RenderPass const renderPass) noexcept :
            myRenderPass (renderPass)
        {}

        template <bool InternalSync>
        friend class RenderPassFactory;

    public:
        RenderPass (RenderPass const&) noexcept = default;
        RenderPass& operator=(RenderPass const&) noexcept = default;

        RenderPass (RenderPass&& other) noexcept :
            myRenderPass (std::exchange (other.myRenderPass, VK_NULL_HANDLE))
        {}

        RenderPass& operator=(RenderPass&& other) noexcept
        {
            if (this == &other) [[unlikely]]
                return *this;

            myRenderPass = std::exchange (other.myRenderPass, VK_NULL_HANDLE);
            return *this;
        }

        [[nodiscard]] bool
        is_valid () const noexcept {
            return myRenderPass;
        }

    private:
        vk::RenderPass myRenderPass;
    };

    template <bool InternalSync> class RenderPassBuilder;
    template <bool InternalSync> class SubpassBuilder;

    template <bool InternalSync>
    class RenderPassFactory final :
        public Detail::CoreDependent
    {
        [[nodiscard]] RenderPass
        make_render_pass (vk::RenderPassCreateInfo const& info)
        {
            vk::raii::RenderPass renderPass { CoreDependent::get_device(), info };

            [[maybe_unused]] auto const lock =
                Detail::lock_mutex <InternalSync, std::lock_guard> (myMutex);

            [[maybe_unused]] auto const [iter, inserted] =
                myRenderPasses.emplace (std::move (renderPass));

            return RenderPass { *iter };
        }

        friend class RenderPassBuilder <InternalSync>;

    protected:
        RenderPassFactory () noexcept = default;

    public:
        explicit RenderPassFactory (std::weak_ptr <Core> const& core) :
            CoreDependent (core)
        {}

        RenderPassFactory (RenderPassFactory&&) = delete;
        RenderPassFactory (RenderPassFactory const&) = delete;

        RenderPassFactory& operator=(RenderPassFactory &&) = delete;
        RenderPassFactory& operator=(RenderPassFactory const&) = delete;

        [[nodiscard]] RenderPassBuilder <InternalSync>
        build_render_pass () noexcept;

        void destroy_render_pass (RenderPass const renderPass) noexcept (!InternalSync)
        {
            assert (renderPass.is_valid());

            [[maybe_unused]] auto const lock =
                Detail::lock_mutex <InternalSync, std::lock_guard> (myMutex);

            myRenderPasses.erase (renderPass.myRenderPass);
        }

    private:
        [[no_unique_address]] Detail::EnableMutex <InternalSync, std::mutex> myMutex;

        boost::unordered::unordered_flat_set
            <vk::raii::RenderPass, Detail::VulkanHash, Detail::VulkanEquals>
        myRenderPasses;
    };

    template <bool InternalSync>
    class RenderPassBuilder final
    {
        [[nodiscard]] static std::pair <vk::PipelineStageFlags, vk::AccessFlags>
        make_stages_access_flags (Detail::SubpassInfo const& info) noexcept
        {
            vk::PipelineStageFlags stages;
            vk::AccessFlags access;

            if (!info.color.empty()) {
                stages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
                access |= vk::AccessFlagBits::eColorAttachmentWrite;
            }

            if (info.depth.has_value())
            {
                stages |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits::eLateFragmentTests;

                access |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            }

            if (!info.input.empty()) {
                stages |= vk::PipelineStageFlagBits::eFragmentShader;
                access |= vk::AccessFlagBits::eInputAttachmentRead;
            }

            if (info.msaa.has_value()) {
                stages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
                access |= vk::AccessFlagBits::eColorAttachmentWrite;
            }

            return { stages, access };
        }

        void add_external_dependencies ()
        {
            assert (!mySubpasses.empty());

            auto const [min, max] = std::ranges::minmax_element (mySubpasses, std::less{},
                [] (auto const& pair) { return pair.second.index; });

            auto const& firstInfo = min->second;
            auto const& lastInfo = max->second;

            auto const [firstStage, firstAccess] = make_stages_access_flags (firstInfo);
            auto const [lastStage, lastAccess] = make_stages_access_flags (lastInfo);

            myDependencies.emplace_back (
                VK_SUBPASS_EXTERNAL,
                firstInfo.index,
                vk::PipelineStageFlagBits::eBottomOfPipe,
                firstStage,
                vk::AccessFlags{},
                firstAccess);

            myDependencies.emplace_back (
                firstInfo.index,
                VK_SUBPASS_EXTERNAL,
                lastStage,
                vk::PipelineStageFlagBits::eBottomOfPipe,
                lastAccess,
                vk::AccessFlags{});
        }

        explicit RenderPassBuilder (
            RenderPassFactory <InternalSync>& factory,
            vk::Format const color,
            vk::Format const depth)
        :
            myFactory (factory),
            myColorFormat (color),
            myDepthFormat (depth),
            mySubpassIndex (0),
            myBaseAttachmentIndex (0)
        {}

        friend class SubpassBuilder <InternalSync>;
        friend class RenderPassFactory <InternalSync>;

    public:
        RenderPassBuilder (RenderPassBuilder&&) noexcept = default;
        RenderPassBuilder (RenderPassBuilder const&) = delete;

        RenderPassBuilder& operator=(RenderPassBuilder &&) noexcept = default;
        RenderPassBuilder& operator=(RenderPassBuilder const&) = delete;

        [[nodiscard]] SubpassBuilder <InternalSync>
        begin_subpass (std::string_view name);

        [[nodiscard]] RenderPass confirm ()
        {
            assert (!mySubpasses.empty());

            boost::container::flat_set <Detail::SubpassInfo,
                decltype([] (auto const& left, auto const& right) { return left.index < right.index; })>
            sortedInfo;

            sortedInfo.reserve (mySubpasses.size());

            for (auto& info : mySubpasses | std::views::values)
                sortedInfo.emplace (std::move (info));

            mySubpasses.clear();

            std::vector <vk::SubpassDescription> descriptions;
            descriptions.reserve (sortedInfo.size());

            for (auto const& info : sortedInfo)
                descriptions.emplace_back (
                    vk::SubpassDescriptionFlags{},
                    vk::PipelineBindPoint::eGraphics,
                    static_cast <uint32_t> (info.input.size()),
                    info.input.data(),
                    static_cast <uint32_t> (info.color.size()),
                    info.color.data(),
                    info.msaa.has_value() ? &*info.msaa : nullptr,
                    info.depth.has_value() ? &*info.depth : nullptr,
                    0, nullptr);

            vk::RenderPassCreateInfo const createInfo {
                {},
                static_cast <uint32_t> (myAttachments.size()),
                myAttachments.data(),
                static_cast <uint32_t> (descriptions.size()),
                descriptions.data(),
                static_cast <uint32_t> (myDependencies.size()),
                myDependencies.data()
            };

            return myFactory.make_render_pass (createInfo);
        }

    private:
        [[nodiscard]] Detail::SubpassInfo const&
        get_subpass_info (std::string_view const name) const
        {
            assert (!name.empty());

            auto const iter = mySubpasses.find (name);
            assert (iter != mySubpasses.end());

            return iter->second;
        }

        [[nodiscard]] vk::Format get_color_format () const noexcept {
            return myColorFormat;
        }

        [[nodiscard]] vk::Format get_depth_format () const noexcept {
            return myDepthFormat;
        }

        void adopt_subpass (
            std::string&& name,
            Detail::SubpassInfo&& info,
            std::vector <vk::AttachmentDescription>&& attachments,
            std::vector <vk::SubpassDependency>&& dependencies)
        {
            myAttachments.reserve (myAttachments.size() + attachments.size());
            myDependencies.reserve (myDependencies.size() + dependencies.size());
            mySubpasses.reserve (mySubpasses.size() + 1);

            myAttachments.insert (myAttachments.end(), attachments.begin(), attachments.end());
            myDependencies.insert (myDependencies.end(), dependencies.begin(), dependencies.end());

            mySubpasses.emplace (std::move (name), std::move (info));

            ++mySubpassIndex;
            myBaseAttachmentIndex += attachments.size();
        }

        RenderPassFactory <InternalSync>& myFactory;

        vk::Format myColorFormat;
        vk::Format myDepthFormat;

        std::vector <vk::AttachmentDescription> myAttachments;
        std::vector <vk::SubpassDependency> myDependencies;

        uint32_t mySubpassIndex;
        uint32_t myBaseAttachmentIndex;

        boost::unordered::unordered_flat_map
            <std::string, Detail::SubpassInfo, Detail::TransparentHash, std::equal_to <>>
        mySubpasses;
    };

    template <bool InternalSync>
    class SubpassBuilder final
    {
        [[nodiscard]] vk::AttachmentDescription
        create_color_attachment (vk::SampleCountFlagBits const samples) const noexcept
        {
            return vk::AttachmentDescription {
                {},
                myTopBuilder.get_color_format(),
                samples,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare,
                vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal
            };
        }

        [[nodiscard]] vk::AttachmentDescription
        create_depth_attachment (vk::SampleCountFlagBits const samples) const noexcept
        {
            return vk::AttachmentDescription {
                {},
                myTopBuilder.get_depth_format(),
                samples,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare,
                vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eDepthStencilAttachmentOptimal
            };
        }

        [[nodiscard]] vk::AttachmentDescription
        create_input_attachment (vk::Format const format) const noexcept
        {
            return vk::AttachmentDescription {
                {},
                format,
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eLoad,
                vk::AttachmentStoreOp::eDontCare,
                vk::AttachmentLoadOp::eDontCare,
                vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal
            };
        }

        [[nodiscard]] vk::AttachmentDescription
        create_msaa_attachment () const noexcept
        {
            return vk::AttachmentDescription {
                {},
                myTopBuilder.get_color_format(),
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eDontCare,
                vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare,
                vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::ePresentSrcKHR
            };
        }

        [[nodiscard]] std::optional<vk::SubpassDependency>
        make_dependency (
            Detail::SubpassInfo const& srcSubpass,
            Detail::SubpassInfo const& dstSubpass) const noexcept
        {
            vk::PipelineStageFlags srcStages;
            vk::AccessFlags srcAccess;

            vk::PipelineStageFlags dstStages;
            vk::AccessFlags dstAccess;

            bool hasDependency = false;

            if (!srcSubpass.color.empty() && !dstSubpass.input.empty())
            {
                srcStages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
                srcAccess |= vk::AccessFlagBits::eColorAttachmentWrite;

                dstStages |= vk::PipelineStageFlagBits::eFragmentShader;
                dstAccess |= vk::AccessFlagBits::eInputAttachmentRead;

                hasDependency = true;
            }

            if (srcSubpass.depth.has_value() && !dstSubpass.input.empty())
            {
                srcStages |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                             vk::PipelineStageFlagBits::eLateFragmentTests;
                srcAccess |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;

                dstStages |= vk::PipelineStageFlagBits::eFragmentShader;
                dstAccess |= vk::AccessFlagBits::eInputAttachmentRead;

                hasDependency = true;
            }

            if (srcSubpass.msaa.has_value() && !dstSubpass.input.empty())
            {
                srcStages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
                srcAccess |= vk::AccessFlagBits::eColorAttachmentWrite;

                dstStages |= vk::PipelineStageFlagBits::eFragmentShader;
                dstAccess |= vk::AccessFlagBits::eInputAttachmentRead;

                hasDependency = true;
            }

            if (!srcSubpass.color.empty() && !dstSubpass.color.empty())
            {
                srcStages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
                srcAccess |= vk::AccessFlagBits::eColorAttachmentWrite;

                dstStages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
                dstAccess |= vk::AccessFlagBits::eColorAttachmentWrite;

                hasDependency = true;
            }

            if (srcSubpass.depth.has_value() && dstSubpass.depth.has_value())
            {
                srcStages |=
                    vk::PipelineStageFlagBits::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits::eLateFragmentTests;

                dstStages |=
                    vk::PipelineStageFlagBits::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits::eLateFragmentTests;

                srcAccess |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
                dstAccess |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;

                hasDependency = true;
            }

            if (!hasDependency) [[unlikely]]
                return std::nullopt;

            return vk::SubpassDependency {
                srcSubpass.index,
                dstSubpass.index,
                srcStages,
                dstStages,
                srcAccess,
                dstAccess,
                {}
            };
        }

        void bake_attachments () noexcept {
            myAttachmentsBaked = true;
        }

        explicit SubpassBuilder (
            RenderPassBuilder <InternalSync>& builder,
            uint32_t const subpassIndex,
            uint32_t const attachmentBaseIndex,
            std::string&& name)
        noexcept :
            myTopBuilder (builder),
            myAttachmentsBaseIndex (attachmentBaseIndex),
            myInfo (subpassIndex, {}, {}, std::nullopt, std::nullopt),
            myName (std::move (name)),
            myAttachmentsBaked (false)
        {}

        friend class RenderPassBuilder <InternalSync>;

    public:
        SubpassBuilder (SubpassBuilder&&) noexcept = default;
        SubpassBuilder (SubpassBuilder const&) = delete;

        SubpassBuilder& operator=(SubpassBuilder &&) noexcept = default;
        SubpassBuilder& operator=(SubpassBuilder const&) = delete;

        [[nodiscard]] SubpassBuilder&
        color_attachment (uint32_t const samples = 1)
        {
            assert (!myAttachmentsBaked);
            assert (std::has_single_bit (samples) && samples <= 64);

            myInfo.color.reserve (myInfo.input.size() + 1);
            myAttachments.reserve (myAttachments.size() + 1);

            auto const attachment = create_color_attachment (static_cast <vk::SampleCountFlagBits> (samples));
            auto const index = myAttachments.size() + myAttachmentsBaseIndex;

            myInfo.color.emplace_back (index, attachment.finalLayout);
            myAttachments.emplace_back (attachment);

            return *this;
        }

        [[nodiscard]] SubpassBuilder&
        depth_attachment (uint32_t const samples = 1)
        {
            assert (!myAttachmentsBaked);
            assert (!myInfo.depth.has_value());
            assert (std::has_single_bit (samples) && samples <= 64);

            auto const attachment = create_depth_attachment (static_cast <vk::SampleCountFlagBits> (samples));
            auto const index = myAttachments.size() + myAttachmentsBaseIndex;

            myAttachments.emplace_back (attachment);
            myInfo.depth.emplace (index, attachment.finalLayout);

            return *this;
        }

        [[nodiscard]] SubpassBuilder&
        msaa_attachment ()
        {
            assert (!myAttachmentsBaked);
            assert (!myInfo.color.empty());

            auto const attachment = create_msaa_attachment ();
            auto const index = myAttachments.size() + myAttachmentsBaseIndex;

            myAttachments.emplace_back (attachment);
            myInfo.msaa.emplace (index, vk::ImageLayout::eColorAttachmentOptimal);
            bake_attachments ();

            return *this;
        }

        [[nodiscard]] SubpassBuilder&
        input_attachment (InputAttachmentFormat const format)
        {
            assert (!myAttachmentsBaked);
            assert (Detail::is_enum_valid (format));

            auto const sourceFormat =
                format == InputAttachmentFormat::color ?
                myTopBuilder.get_color_format () :
                myTopBuilder.get_depth_format ();

            myInfo.input.reserve (myInfo.input.size() + 1);
            myAttachments.reserve (myAttachments.size() + 1);

            auto const attachment = create_input_attachment (sourceFormat);
            auto const index = myAttachments.size() + myAttachmentsBaseIndex;

            myInfo.input.emplace_back (index, attachment.finalLayout);
            myAttachments.emplace_back (attachment);

            return *this;
        }

        [[nodiscard]] SubpassBuilder&
        depend_on (std::string_view const waitSubpass)
        {
            assert (!waitSubpass.empty());

            auto const& source = myTopBuilder.get_subpass_info (waitSubpass);
            auto const dependency = make_dependency (myInfo, source);

            if (dependency.has_value()) [[likely]]
                myDependencies.emplace_back (*dependency);

            bake_attachments ();
            return *this;
        }

        [[nodiscard]] RenderPassBuilder <InternalSync>&
        end_subpass ()
        {
            assert (!myInfo.msaa.has_value() || !myInfo.color.empty());
            assert (!myAttachments.empty());

            auto& lastAttachment = myAttachments.back();

            if (lastAttachment.finalLayout == vk::ImageLayout::eColorAttachmentOptimal)
                lastAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

            myTopBuilder.adopt_subpass (
                std::move (myName),
                std::move (myInfo),
                std::move (myAttachments),
                std::move (myDependencies));

            return myTopBuilder;
        }

    private:
        RenderPassBuilder <InternalSync>& myTopBuilder;

        uint32_t myAttachmentsBaseIndex;
        std::vector <vk::AttachmentDescription> myAttachments;
        std::vector <vk::SubpassDependency> myDependencies;

        Detail::SubpassInfo myInfo;
        std::string myName;

        bool myAttachmentsBaked;
    };

    template <bool InternalSync>
    RenderPassBuilder <InternalSync> RenderPassFactory <InternalSync>
    ::build_render_pass() noexcept
    {
        return RenderPassBuilder <InternalSync> { *this, vk::Format::eB8G8R8A8Srgb, vk::Format::eD16Unorm };
    }

    template <bool InternalSync>
    SubpassBuilder <InternalSync> RenderPassBuilder <InternalSync>
    ::begin_subpass (std::string_view const name)
    {
        return SubpassBuilder { *this, mySubpassIndex, myBaseAttachmentIndex, std::string{ name }  };
    }
}

#endif