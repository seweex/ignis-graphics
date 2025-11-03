
#include <concepts>
#include <type_traits>
#include <functional>

#include <vulkan/vulkan_raii.hpp>

namespace Kygo::Detail
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

		[[nodiscard]] static auto const&
		get_handle (Ty const& handle) noexcept
		{
			if constexpr (IsRaii::value)
				return static_cast <typename Ty::CppType> (handle);
			else
				return handle;
		}

		[[nodiscard]] static auto const&
		get_native (Ty const& handle) noexcept
		{
			if constexpr (IsRaii::value)
				return static_cast <typename Ty::CType> (*handle);
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
			using Traits = VulkanHandleTraits <Handle>;

			auto const leftHandle = Traits::get_handle(handle);
			auto const rightHandle = Traits::get_handle(handle);

			return leftHandle == rightHandle;
		}
	};
}