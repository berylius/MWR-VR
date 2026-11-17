#pragma once
#include "pose_filter.hpp"

namespace vr::mounted_camera_transport
{
	inline pose_filter::pose translation_frame(pose_filter::pose carrier) noexcept
	{
		// A ground turret's aim may rotate its root too. Carry the seat with its
		// base translation, never with the barrel or native orbiting player body.
		carrier.orientation=pose_filter::identity;return carrier;
	}
	// Freeze the seat in its carrier, never in the world or the aiming gun.
	class seat_anchor
	{
		bool valid_{};
		std::uint64_t owner_{},revision_{};
		std::uintptr_t object_{};
		int time_{};
		pose_filter::vec local_{};
	public:
		void reset() noexcept {valid_=false;}
		std::uint64_t revision() const noexcept {return revision_;}
		bool update(std::uint64_t owner,std::uintptr_t object,int time,const pose_filter::pose& carrier,
			pose_filter::vec native_eye,pose_filter::vec& eye) noexcept
		{
			if(!pose_filter::valid(carrier) || !pose_filter::valid({native_eye}))
			{reset();return false;}
			if(!valid_ || owner!=owner_ || object!=object_ || time<time_)
			{
				// Only acquisition needs a nearby native eye. Linked/native view
				// origins may stay at the entry point while their carrier moves.
				if(pose_filter::length(pose_filter::sub(native_eye,carrier.position))>512.f)
				{reset();return false;}
				local_=pose_filter::compose(pose_filter::inverse(carrier),{native_eye}).position;
				valid_=true;owner_=owner;object_=object;++revision_;
			}
			time_=time;eye=pose_filter::compose(carrier,{local_}).position;return true;
		}
	};
}
