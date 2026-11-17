#pragma once
#include <cstdint>
namespace vr::ui_texture_limits
{
	inline constexpr std::uint32_t maximum_dimension = 16384;
	template <class Number> constexpr bool valid_extent(Number width, Number height) noexcept
	{
		return width > 0 && height > 0 && width <= maximum_dimension && height <= maximum_dimension;
	}
}
