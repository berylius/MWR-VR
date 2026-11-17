#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace scene_models
{
	// Native rigid-model filter has four view banks: camera, then shadow views.
	// Reclassify only a valid rejected camera; retain all native shadow results.
	inline unsigned preserve_primary_visibility(std::span<std::uint8_t> banks,unsigned rejected,bool requested) noexcept
	{
		if(requested && banks.size()==4 && banks[0]==2 && rejected>0 && rejected<=4)
		{banks[0]=1;return rejected-1;}
		return rejected;
	}
	// Native radius is multiplied by placement scale after the call. Keep a
	// finite world-space allowance even for malformed/tiny scales and requests.
	inline float local_radius_padding(float world_padding, float scale) noexcept
	{
		return std::isfinite(world_padding) && std::isfinite(scale) && scale >= .0001f ?
			std::clamp(world_padding,0.f,128.f)/scale : 0.f;
	}
}
