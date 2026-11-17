#include "component/vr/native_aa_sample.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include <cassert>
#include <iostream>

int main()
{
	using namespace vr;
	for(std::uint64_t pair=1;pair<120;++pair)
	{
		// Arbitrarily repeated/skipped native samples must share the backend phase.
		for(const auto native: {std::array{-.00025f,.0004f},std::array{.00025f,-.0004f}})
		{
			const auto a=native_post_aa::sample_jitter(native,pair);
			const auto b=native_post_aa::sample_jitter(native,pair+1);
			assert(a[0]==-b[0] && a[1]==-b[1]);
			assert((a[0]>0)==bool(native_post_aa::sample_phase(pair)&1));
			assert(std::abs(a[0])==std::abs(native[0]) && std::abs(a[1])==std::abs(native[1]));
		}
		assert((native_post_aa::sample_jitter({0,0},pair)==std::array<float,2>{0,0}));
	}
	namespace w=gameplay::weapons;
	for(const auto* ak:{&w::ak47::physical,&w::ak47::arctic,&w::ak47::digital,&w::ak47::desert,&w::ak47::woodland})
	{
		const auto original=ak->interaction_for(true),current=ak->interaction_for(false);
		assert(current.rock && !original.rock && ak->interaction.rock);
		assert(original.manual_magazine==current.manual_magazine);
		assert(original.slide_stroke==current.slide_stroke && original.slide_stroke==.125f);
		// Exact immutable profile/asset identities stay intact.
		assert(ak->native_name=="ak47" && original.well_radius==current.well_radius);
	}
	assert(w::m4::physical.interaction_for(true).rock==w::m4::physical.interaction.rock);
	// Drive the real gesture and ammunition transaction: a straight approach
	// seats only in original mode, with identical round accounting on both sides.
	for(bool original:{false,true})for(auto rear:{hand::left,hand::right})
	{
		namespace p=w::physical_reload;namespace m=w::mechanics;
		const auto rules=w::ak47::physical.ammunition;
		const auto tuning=w::ak47::physical.interaction_for(original);
		m::state ammo{49,1,1,true,true,29,60};
		w::hold owner{49,1,rear,hand::none,w::hold_source::engine_default,1};
		controller_input::frame input;input.sequence=input.reference_generation=1;input.focused=true;
		for(int h=0;h<2;++h){input.grip[h].valid=input.aim[h].valid=true;input.trigger[h]=input.secondary[h]={true,false,0,1};}
		p::geometry geometry{true,49,1,1,1,1,1,1,{}, {0,0,-.2f}};
		geometry.magazine.valid=true;geometry.magazine.grip_distance=1;
		p::controller gesture;auto now=p::clock::time_point{}+std::chrono::seconds(1);
		const auto step=[&]{now+=std::chrono::milliseconds(10);input.sampled_at=now;geometry.input_sequence=++input.sequence;
			gesture.update(tuning,rules,ammo,input,owner,geometry,true,now,[](const auto&){return true;});};
		step();
		const auto off=rear==hand::left?hand::right:hand::left;
		const auto remove=m::plan(rules,ammo,{m::operation::pull_magazine,49,1,ammo.revision,rear,off});
		assert(remove);ammo=remove.next;
		const auto total=m::total_rounds(ammo);
		input.trigger[int(off)].down=true;++input.trigger[int(off)].presses;step();
		geometry.magazine_top_in_well={0,0,-.04f};step();
		assert(ammo.magazine_inserted==original && m::total_rounds(ammo)==total);
		assert(original?ammo.magazine_hand==hand::none:ammo.magazine_hand==off);
	}
	std::cout<<"MOUNTED_AA_AK_TESTS_OK\n";
}
