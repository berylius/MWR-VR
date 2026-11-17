// Reuse the real native-ammunition transaction fixture; legacy storage tests
// are intentionally not executed by this focused regression.
#define main existing_reload_suite_main
#include "physical_reload_tests.cpp"
#undef main
#include "reload_attachment_tests.hpp"
#include "component/vr/gameplay/weapons/mwr_shared.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	bool original_ak_attachment() noexcept {return false;}
}

int main()
{
	unsigned failures{},cases{};
	const auto check=[&](bool ok,const char* text){if(!ok){++failures;std::cerr<<text<<'\n';}};
	for(const auto* definition:{&w::m4::physical,&w::ak47::physical,&w::mwr_shared::m4_reload,&w::mwr_shared::ak47_reload})
	for(auto rear:{vr::hand::left,vr::hand::right})
	for(int interruption=0;interruption<8;++interruption)
	{
		fixture f(definition);f.owner.rear=rear;f.input.continuity_generation=10;f.input.retention_generation=5;f.step();
		const auto off=1-int(rear);
		f.geometry.waist_distance=.01f;f.trigger(true);
		check(f.state.magazine_hand==vr::hand(off) && f.state.held_rounds>0,"fixture owns a real counted magazine");
		const auto total=m::total_rounds(f.state);const auto held=f.state.held_rounds;
		const auto before=f.native;const auto commits=f.commits;
		f.now+=3200ms;++f.input.continuity_generation;
		// Digital samplers still fence edges after a stall. Retention may not
		// re-enable fire or consume a synthetic post-gap press as insertion.
		++f.input.trigger[off].generation;++f.input.trigger[off].presses;
		f.geometry.magazine_top_in_well={0,0,0};
		switch(interruption)
		{
		case 1:++f.input.retention_generation;break; // Real tracking/focus lifetime changed.
		case 2:f.input.trigger[off].down=false;break;
		case 3:f.input.grip[off].valid=false;break;
		case 4:f.manipulation=false;break;
		case 5:++f.owner.rear_revision;break;
		case 6:++f.input.reference_generation;f.geometry.reference_generation=f.input.reference_generation;break;
		case 7:f.input.focused=false;break;
		}
		f.step();
		check(m::total_rounds(f.state)==total,"all interruption paths conserve magazine/chamber/reserve rounds");
		if(!interruption)
		{
			check(f.state.held_rounds==held && f.native==before && f.commits==commits,"timing-only stall retains the exact escrow without a transaction");
			check(f.control.requires_withdrawal(),"resume cannot bridge old and new insertion contacts");
			f.step();check(f.state.held_rounds==held && f.native==before,"next stable frame retains the magazine without replaying edges");
			f.trigger(false);check(f.state.magazine_hand==vr::hand::none,"a subsequent physical release still returns the magazine");
		}
		else check(f.state.magazine_hand==vr::hand::none,"real ownership/focus/tracking/release changes still cancel");
		++cases;
	}
	for(const auto* definition:{&w::m4::physical,&w::ak47::physical,&w::mwr_shared::m4_reload,&w::mwr_shared::ak47_reload})
	for(auto rear:{vr::hand::left,vr::hand::right})
	for(int interruption=0;interruption<8;++interruption)
	{
		fixture f(definition);f.owner.rear=rear;f.input.continuity_generation=10;f.input.retention_generation=5;f.step();
		const auto off=1-int(rear);f.geometry.waist_distance=.01f;f.trigger(true);
		const auto held=f.state.held_rounds;const auto before=f.native;const auto commits=f.commits;
		f.now+=50ms; // Input is continuous; only the camera/body publication stalls.
		switch(interruption){case 1:++f.input.retention_generation;break;case 2:f.input.trigger[off].down=false;break;case 3:f.input.grip[off].valid=false;break;case 4:++f.owner.rear_revision;break;case 5:++f.input.reference_generation;break;case 6:f.input.focused=false;break;case 7:f.input.orientation_settling=true;break;}
		const bool retained=f.control.suspend_magazine(f.state,f.input,f.owner,f.now);
		check(retained==!interruption,"pose-only suspension retains an existing pinch, while real interruptions reject");
		if(retained)
		{
			check(f.state.held_rounds==held && f.native==before && f.commits==commits,"pose wait neither refunds escrow nor changes native ammo");
			f.geometry.magazine_top_in_well={0,0,0};f.step();
			check(f.state.held_rounds==held && f.native==before && f.control.requires_withdrawal(),"fresh resumption rebases motion without inserting across the missing body frame");
			f.trigger(false);check(f.state.magazine_hand==vr::hand::none,"release after pose wait still settles the real magazine");
		}
		++cases;
	}
	vr::controller_input::frame before;before.sequence=2;before.reference_generation=3;before.focused=true;before.sampled_at=p::clock::time_point{1s};
	auto after=before;++after.sequence;after.sampled_at+=3200ms;
	check(vr::controller_input::producer_discontinuity(before,after) && !vr::controller_input::producer_retention_discontinuity(before,after),"a delay fences motion but retains item ownership");
	for(int hard=0;hard<6;++hard)
	{
		auto changed=after;
		switch(hard){case 0:changed.focused=false;break;case 1:++changed.reference_generation;break;case 2:++changed.pose_reference_generation;break;case 3:changed.grip[0].valid=true;break;case 4:changed.orientation_settling=true;break;case 5:changed.sampled_at=before.sampled_at-1ms;break;}
		check(vr::controller_input::producer_retention_discontinuity(before,changed),"hard producer boundary fences retained ownership");
	}
	reload_attachment_tests::run(check);
	button_magazine_tests::run<fixture>(check);
	std::cout<<"INPUT_STALL_RELOAD cases="<<cases<<" failures="<<failures<<'\n';
	return failures?1:0;
}
