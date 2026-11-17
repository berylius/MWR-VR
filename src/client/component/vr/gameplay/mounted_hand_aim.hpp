#pragma once
#include "../controller_input.hpp"
#include "../head_pose_bridge.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Mounted and vehicle guns (airlift Mk19, MG turrets): the native viewmodel hands
// always hold the spade grips (user: no grab step). The gun swings about a pintle
// ahead of the tracked hands. The muzzle follows the line from the rear grips
// through the pivot; vertical input keeps the same rear-grip motion. A hand losing
// tracking re-anchors without a jump.
// All in the HMD tracking reference (H2 axes: forward, left, up), so
// the gun's native view never feeds back into the hands.
namespace vr::gameplay::mounted_hand_aim
{
	using vec = head_pose_bridge::vector3;
	inline constexpr float pintle_meters = .5f; // Mk19 spade grips to pintle.
	inline constexpr float handle_forward_meters = .4f, handle_below_meters = .3f;
	inline constexpr float degrees = 57.29577951308232f;
	inline vec local_position(const head_pose_bridge::tracking_pose& reference, const vec& tracking) noexcept
	{
		vec d{}, t{};
		for (int i = 0; i < 3; ++i) d[i] = tracking[i] - reference.position_meters[i];
		for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) t[i] += reference.orientation[j][i] * d[j];
		return {-t[2], -t[0], t[1]};
	}
	inline vec direction(float pitch, float yaw) noexcept
	{
		const float p = pitch / degrees, y = yaw / degrees;
		return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
	}
	class state
	{
	public:
		// Returns false when no aim is available (falls back to the head).
		bool update(const controller_input::frame& input, bool mounted, std::array<float, 2>& aim,
			std::uint64_t owner = 1) noexcept
		{
			if (!mounted || !owner) {*this = {};return false;}
			if (owner_ != owner) {*this = {};owner_ = owner;}
			head_pose_bridge::spatial_frame frame;
			if (!input.focused || !head_pose_bridge::get_spatial_frame(frame) || frame.generation != input.reference_generation)
				return valid_ && (aim = aim_, true);
			if (!valid_ || generation_ != frame.generation)
			{
				// Entry (or recenter): one neutral handle centre below/in front of
				// the head. Looking down at the gun must not tilt the input rig.
				const auto head = head_pose_bridge::get_status();
				const auto forward = head.local_orientation[0];
				const float horizontal = std::hypot(forward[0], forward[1]);
				if (!(horizontal > .05f) || !(frame.units_per_meter > 0.f)) return false;
				aim_ = {0.f, std::atan2(forward[1], forward[0]) * degrees};
				for (unsigned i=0;i<3;++i) neutral_[i]=head.local_position_units[i]/frame.units_per_meter;
				neutral_[0]+=forward[0]/horizontal*handle_forward_meters;
				neutral_[1]+=forward[1]/horizontal*handle_forward_meters;
				neutral_[2]-=handle_below_meters;
				valid_ = true;generation_ = frame.generation;held_ = 0;sent_valid_=false;
			}
			unsigned held{};vec center{};
			for (int h = 0; h < 2; ++h)
			{
				if (!input.grip[h].valid) continue;
				const auto p = local_position(frame.reference, input.grip[h].tracking.position_meters);
				if (!std::isfinite(p[0] + p[1] + p[2])) continue;
				held |= 1u << h;
				for (int i = 0; i < 3; ++i) center[i] += p[i];
			}
			const int count = (held & 1) + ((held >> 1) & 1);
			if (count) for (auto& x : center) x /= float(count);
			if (held != held_)
			{
				// Normalize even off-centre hands to the fixed neutral handles.
				// Tracking changes rebind there on the current aim without a jump.
				held_ = held;
				if (count)
				{
					const auto d = direction(aim_[0], aim_[1]);
					for (int i = 0; i < 3; ++i)
					{
						hand_offset_[i]=neutral_[i]-center[i];
						pintle_[i]=neutral_[i]+d[i]*pintle_meters;
					}
				}
			}
			else if (count)
			{
				vec d{};
				for (int i = 0; i < 3; ++i) d[i] = pintle_[i] - (center[i]+hand_offset_[i]);
				const float horizontal = std::hypot(d[0], d[1]);
				if (horizontal > .1f)
				{
					const float pitch = std::atan2(d[2], horizontal) * degrees;
					// Spade grips sit BEHIND the pintle: the muzzle continues from
					// the hands through the pivot. Reflecting this yaw reverses the
					// actual gun when the player swings its rear grips sideways.
					const float yaw = std::atan2(d[1],d[0])*degrees;
					if (std::isfinite(pitch) && std::isfinite(yaw)) aim_ = {std::clamp(pitch, -80.f, 80.f), yaw};
				}
			}
			aim = aim_;
			return true;
		}
		unsigned held() const noexcept {return held_;}
		vec neutral_center() const noexcept {return neutral_;}
		vec hand_offset() const noexcept {return hand_offset_;}
		// Native pitch steps with the aim instead of being set absolutely. The
		// vehicle clamps its pitch arc through delta_angles; an absolute request
		// past the arc re-subtracted the grown delta every command and ran away
		// (airlift mock: -2500 deg/s). Native pitch positive looks down.
		float pitch_step(float native_before, float aim_pitch) noexcept
		{
			if (!sent_valid_) {sent_pitch_ = aim_pitch;sent_valid_ = true;}
			const float result = std::remainder(native_before - (aim_pitch - sent_pitch_), 360.f);
			sent_pitch_ = aim_pitch;
			return result;
		}
	private:
		bool sent_valid_{};
		float sent_pitch_{};
		bool valid_{};
		std::uint64_t generation_{},owner_{};
		unsigned held_{};
		vec pintle_{},neutral_{},hand_offset_{};
		std::array<float, 2> aim_{};
	};
}
