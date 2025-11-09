#ifndef IGNIS_DETAIL_VULKAN_FUNCTIONAL_HXX
#define IGNIS_DETAIL_VULKAN_FUNCTIONAL_HXX

#include <concepts>
#include <type_traits>
#include <functional>

#include <vulkan/vulkan_raii.hpp>

namespace Ignis::Detail
{
	template <class Ty>
	class VulkanHandleTraits final
	{
	public:
		using IsRaii = std::bool_constant <
			requires (Ty& handle)
			{
				typename Ty::CppType;

				handle.clear();
				{ handle.release() } -> std::same_as <typename Ty::CppType>;
			}>;

		using IsUniqueHandle = std::bool_constant <
			requires (Ty& handle)
			{
				typename Ty::element_type;
				handle.get();
			}>;

		[[nodiscard]] static auto
		get_handle (Ty const& handle) noexcept
		{
			if constexpr (IsRaii::value)
				return static_cast <typename Ty::CppType> (handle);

			else if constexpr (IsUniqueHandle::value)
				return handle.get();

			else
				return handle;
		}

		[[nodiscard]] static auto
		get_native (Ty const& handle) noexcept
		{
			if constexpr (IsRaii::value)
				return static_cast <typename Ty::CType> (*handle);

			else if constexpr (IsUniqueHandle::value)
				return static_cast <typename Ty::element_type::CType> (handle.get());

			else
				return static_cast <typename Ty::CType> (handle);
		}

		[[nodiscard]] static bool
		is_valid (Ty const& handle) noexcept {
			return handle != VK_NULL_HANDLE;
		}
	};

	class VulkanHash final
	{
	public:
		using is_transparent = std::true_type;

		template <class Handle>
		[[nodiscard]] size_t operator() (Handle const& handle) const noexcept
		{
			using Traits = VulkanHandleTraits <Handle>;

			void const* const native = Traits::get_native (handle);
			std::hash <void const*> constexpr hasher;

			return hasher (native);
		}
	};

	class VulkanEquals final
	{
	public:
		using is_transparent = std::true_type;

		template <class LeftHandle, class RightHandle>
		[[nodiscard]] bool operator() (LeftHandle const& left, RightHandle const& right) const noexcept
		{
			using LeftTraits = VulkanHandleTraits <LeftHandle>;
			using RightTraits = VulkanHandleTraits <RightHandle>;

			auto const leftHandle = LeftTraits::get_handle (left);
			auto const rightHandle = RightTraits::get_handle (right);

			return leftHandle == rightHandle;
		}
	};
}

#endif
