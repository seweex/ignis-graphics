#ifndef IGNIS_DETAIL_VULKAN_FUNCTIONAL_HXX
#define IGNIS_DETAIL_VULKAN_FUNCTIONAL_HXX

#include <concepts>
#include <type_traits>
#include <functional>

#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_hash.hpp>

namespace Ignis::Detail
{
	template <class Ty>
	concept VulkanHashableHandle = requires (Ty const& handle)
	{
		{ std::hash <Ty> {} (handle) } -> std::same_as <size_t>;
	};

	template <class Ty>
	concept VulkanRaiiHandle = requires (Ty& handle)
	{
		typename Ty::CppType;
		requires std::convertible_to <Ty, typename Ty::CppType>;

		handle.clear();
		{ handle.release() } -> std::same_as <typename Ty::CppType>;
	};

	template <class Ty>
	concept VulkanUniqueHandle = requires (Ty const& handle)
	{
		typename Ty::element_type;
		{ handle.get() } -> std::same_as <typename Ty::element_type const&>;
	};

	template <class Ty>
	class VulkanHandleTraits final
	{
	public:
		[[nodiscard]] static auto
		get_handle (Ty const& handle) noexcept
		{
			if constexpr (VulkanRaiiHandle <Ty>)
				return static_cast <typename Ty::CppType> (handle);

			else if constexpr (VulkanUniqueHandle <Ty>)
				return handle.get();

			else
				return handle;
		}

		[[nodiscard]] static auto
		get_native (Ty const& handle) noexcept
		{
			if constexpr (VulkanRaiiHandle <Ty>) {
				auto const unowningHandle = static_cast <typename Ty::CppType> (handle);
				return static_cast <typename Ty::CppType::CType> (unowningHandle);
			}

			else if constexpr (VulkanUniqueHandle <Ty>) {
				auto const unowningHandle = handle.get();
				return static_cast <typename Ty::element_type::CType> (unowningHandle);
			}

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
			if constexpr (VulkanHashableHandle <Handle>)
				return std::hash <Handle> {} (handle);

			else {
				void const* const native = VulkanHandleTraits <Handle>::get_native (handle);

				return std::hash <void const*> {} (native);
			}
		}
	};

	class VulkanEquals final
	{
	public:
		using is_transparent = std::true_type;

		template <class LeftHandle, class RightHandle>
		[[nodiscard]] bool operator() (LeftHandle const& left, RightHandle const& right) const noexcept
		{
			if constexpr (std::equality_comparable_with <LeftHandle, RightHandle>)
				return left == right;

			else {
				using LeftTraits = VulkanHandleTraits <LeftHandle>;
				using RightTraits = VulkanHandleTraits <RightHandle>;

				auto const leftHandle = LeftTraits::get_handle (left);
				auto const rightHandle = RightTraits::get_handle (right);

				return leftHandle == rightHandle;
			}
		}
	};
}

#endif
