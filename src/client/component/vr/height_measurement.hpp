#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace vr::player_height::measurement
{
	struct pose {float span{}, drop{};};
	// Positions in one physical, metre-based frame. Admission follows the arm
	// span rather than the recenter yaw, so turning before measuring is safe.
	inline std::optional<pose> tpose(const std::array<float,3>& head,
		const std::array<float,3>& left,const std::array<float,3>& right) noexcept
	{
		for(const auto& point : {head,left,right}) for(float x:point) if(!std::isfinite(x))return {};
		const float span=std::hypot(left[0]-right[0],left[1]-right[1]);
		const float drop=head[2]-(left[2]+right[2])*.5f;
		const float midpoint=std::hypot((left[0]+right[0])*.5f-head[0],(left[1]+right[1])*.5f-head[1]);
		if(span<=.9f || span>=2.4f || std::abs(left[2]-right[2])>=.15f ||
			drop<=.05f || drop>=.45f || midpoint>=.20f)return {};
		return pose{span,drop};
	}
	enum class state {idle,release,pose,holding,floor,tracking,model,complete};
	struct sample {float eye{},span{},drop{};};
	class hold
	{
		float seconds_{};
		sample average_{};
		bool released_{};
	public:
		static constexpr float duration=2.f;
		void reset() noexcept {seconds_=0;average_={};released_=false;}
		void interrupt() noexcept {seconds_=0;average_={};}
		float progress() const noexcept {return seconds_/duration;}
		sample average() const noexcept {return average_;}
		state update(bool left,bool right,state admission,sample value,float elapsed) noexcept
		{
			if(!left && !right)released_=true;
			if(!released_){interrupt();return state::release;}
			if(admission!=state::holding){interrupt();return admission;}
			if(!left || !right){interrupt();return state::pose;}
			// Only fresh consecutive input time counts, never a pause or stale frame.
			if(!std::isfinite(elapsed) || elapsed<0 || elapsed>.15f){interrupt();return state::tracking;}
			elapsed=std::min(elapsed,duration-seconds_);
			const float next=seconds_+elapsed;
			if(next>0)
			{
				const float w=elapsed/next;
				average_.eye+=(value.eye-average_.eye)*w;
				average_.span+=(value.span-average_.span)*w;
				average_.drop+=(value.drop-average_.drop)*w;
			}
			seconds_=next;
			return seconds_>=duration ? state::complete:state::holding;
		}
	};
}
