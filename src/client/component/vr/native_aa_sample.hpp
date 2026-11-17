#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace vr::native_post_aa
{
	// Shared by frontend projection samples and backend per-eye history reads.
	inline constexpr std::uint32_t sample_phase(std::uint64_t pair) noexcept
	{return static_cast<std::uint32_t>(pair & 0x3fffffffull);}
	inline std::array<float,2> sample_jitter(std::array<float,2> natural,std::uint64_t pair) noexcept
	{
		if(!std::isfinite(natural[0]) || !std::isfinite(natural[1]))return natural;
		// H1 0x14058D4D0: {-0.25,+0.25}, {+0.25,-0.25}, scaled by
		// the native scene viewport. Zero jitter (non-temporal/cinematic) stays zero.
		const float sign=(sample_phase(pair)&1u)?1.f:-1.f;
		return {std::abs(natural[0])*sign,-std::abs(natural[1])*sign};
	}
}
