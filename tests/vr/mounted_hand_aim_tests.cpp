#include "component/vr/gameplay/mounted_hand_aim.hpp"
#include "component/vr/gameplay/native_viewmodel_policy.hpp"
#include <cassert>
#include <iostream>

namespace
{
	vr::head_pose_bridge::spatial_frame spatial;
	vr::head_pose_bridge::status tracking;
}
namespace vr::head_pose_bridge
{
	bool get_spatial_frame(spatial_frame& out) noexcept {out=spatial;return true;}
	status get_status() noexcept {return tracking;}
}
int main()
{
	using namespace vr;
	// The turret owns its hands even with a retained personal/native weapon.
	const gameplay::weapons::hold retained{101,7,hand::right};
	assert(!gameplay::hands::model_pose_owner(retained,true,true).weapon);
	assert(gameplay::hands::model_pose_owner(retained,false,true).id()==retained.id());
	assert(gameplay::hands::model_pose_owner(retained,true,false).id()==retained.id());
	spatial.reference.orientation=pose_filter::identity;spatial.generation=1;spatial.units_per_meter=39.37f;
	const auto near=[](float a,float b){return std::abs(std::remainder(a-b,360.f))<.01f;};
	for(float heading:{0.f,45.f,179.f,-179.f})
	{
		const float angle=heading/gameplay::mounted_hand_aim::degrees,c=std::cos(angle),s=std::sin(angle);
		tracking.local_orientation={{{c,s,0},{-s,c,0},{0,0,1}}};
		controller_input::frame input;input.focused=true;input.reference_generation=1;
		for(unsigned h=0;h<2;++h){input.grip[h].valid=true;input.grip[h].tracking.position_meters={h?.2f:-.2f,1.3f,-.4f};}
		gameplay::mounted_hand_aim::state aim;std::array<float,2> out{};
		assert(aim.update(input,true,out) && near(out[1],heading));
		assert(aim.update(input,true,out) && near(out[1],heading));
		// Move both hands to the left relative to the acquired heading.
		const auto rest=input;
		for(auto& hand:input.grip){hand.tracking.position_meters[0]-=c*.1f;hand.tracking.position_meters[2]+=s*.1f;}
		assert(aim.update(input,true,out));
		// Rear grips move left of the pivot; the muzzle points right of it.
		assert(std::remainder(out[1]-heading,360.f)<-10.f);
		input=rest;
		for(auto& hand:input.grip){hand.tracking.position_meters[0]+=c*.1f;hand.tracking.position_meters[2]-=s*.1f;}
		assert(aim.update(input,true,out));
		assert(std::remainder(out[1]-heading,360.f)>10.f);
		const float held=out[1];input.grip[0].valid=false;
		assert(aim.update(input,true,out) && near(out[1],held));
		assert(aim.update(input,true,out) && near(out[1],held));
		input.grip[0].valid=true;input.grip[0].tracking.position_meters[0]-=.5f;
		assert(aim.update(input,true,out) && near(out[1],held));
		assert(!aim.update(input,false,out));
	}
	tracking.local_orientation=pose_filter::identity;
	controller_input::frame input;input.focused=true;input.reference_generation=1;
	for(auto& hand:input.grip){hand.valid=true;hand.tracking.position_meters={0,1.3f,-.4f};}
	gameplay::mounted_hand_aim::state aim;std::array<float,2> out{};
	assert(aim.update(input,true,out));
	for(auto& hand:input.grip)hand.tracking.position_meters[1]-=.1f;
	assert(aim.update(input,true,out) && out[0]>10.f && near(out[1],0));
	assert(near(aim.pitch_step(5,out[0]),5));
	assert(near(aim.pitch_step(5,out[0]+3),2));
	// Mount with head and hands displaced to either side of the old play-space
	// centre. Equal subsequent hand travel must give exactly the same aim.
	for(float side:{-.6f,0.f,.6f})
	{
		tracking.local_position_units={.2f*39.37f,-side*39.37f,.1f*39.37f};
		tracking.local_orientation={{{.8660254f,0,-.5f},{0,1,0},{.5f,0,.8660254f}}};
		input.focused=true;input.reference_generation=spatial.generation;
		for(unsigned h=0;h<2;++h)
		{
			input.grip[h].valid=true;
			input.grip[h].tracking.position_meters={side+(h?.2f:-.2f),1.2f,-.3f};
		}
		gameplay::mounted_hand_aim::state centered;
		assert(centered.update(input,true,out,553) && near(out[0],0) && near(out[1],0));
		const auto neutral=centered.neutral_center(),correction=centered.hand_offset();
		assert(std::abs(neutral[0]-.6f)<.001f && std::abs(neutral[1]+side)<.001f && std::abs(neutral[2]+.2f)<.001f);
		// Measured centre + correction is the below-head neutral handle centre.
		const auto measured=gameplay::mounted_hand_aim::local_position(spatial.reference,
			{side,1.2f,-.3f});
		for(unsigned i=0;i<3;++i)assert(std::abs(measured[i]+correction[i]-neutral[i])<.001f);
		for(auto& hand:input.grip)hand.tracking.position_meters[0]+=.1f;
		assert(centered.update(input,true,out,553) && near(out[1],11.309932f));
		// Looking/moving the head later must never move the fixed input rig.
		tracking.local_position_units={100,200,300};tracking.local_orientation=pose_filter::identity;
		assert(centered.update(input,true,out,553) && near(out[1],11.309932f));
		assert(centered.neutral_center()==neutral && centered.hand_offset()==correction);
		input.focused=false;for(auto& hand:input.grip)hand.tracking.position_meters[0]+=.4f;
		assert(centered.update(input,true,out,553) && near(out[1],11.309932f));
		input.focused=true;input.grip[0].valid=false;
		assert(centered.update(input,true,out,553) && near(out[1],11.309932f));
		// A different gun, or a recenter, acquires a fresh neutral once.
		assert(centered.update(input,true,out,962) && near(out[1],0));
		assert(centered.neutral_center()!=neutral);
		tracking.local_position_units={};++spatial.generation;input.reference_generation=spatial.generation;
		assert(centered.update(input,true,out,962) && near(out[1],0));
		assert(std::abs(centered.neutral_center()[2]+.3f)<.001f);
		assert(!centered.update(input,false,out,962));
	}
	std::cout<<"MOUNTED_HAND_AIM_OK\n";
}
