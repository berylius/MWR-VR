#pragma once
#include <algorithm>
#include <cmath>

namespace vr::projection_bounds
{
	inline bool valid(float left, float right, float down, float up) noexcept
	{
		const auto width = right - left, height = up - down;
		return std::isfinite(left) && std::isfinite(right) && std::isfinite(down) && std::isfinite(up) &&
			std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0 &&
			std::isfinite(2.f / width) && std::isfinite(2.f / height) &&
			2.f / width > 0 && 2.f / height > 0 &&
			std::isfinite(left / width + right / width) && std::isfinite(down / height + up / height);
	}
}
