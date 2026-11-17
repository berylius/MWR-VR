#include "component/vr/native_post_aa_contract.hpp"
#include "component/vr/native_post_aa_profile.hpp"
#include <cassert>

int main()
{
	using namespace vr::native_post_aa;
	assert(!saved_menu_mode("// seta r_postAA \"FXAA\"\n"));
	assert(saved_menu_mode("seta 0x2cf72d9e \"SMAA T2x\"\r\n")==3);
	assert(saved_menu_mode("seta r_postAA \"None\"\nseta R_POSTAA \"Filmic SMAA T2x\"\n")==5);
	assert(saved_menu_mode("set r_postAA 2\n")==2);
	assert(saved_menu_mode("seta r_postAA \"None\"\n")==0);
	assert(!saved_menu_mode("seta r_postAA \"bogus\"\n"));
	assert(!saved_menu_mode("seta r_postAA_extra \"SMAA T2x\"\n"));
	// Every menu mode runs natively in VR (no SMAA 1x substitution).
	for (const auto selected : {mode::none, mode::fxaa, mode::smaa, mode::smaa_t2x, mode::filmic_smaa, mode::filmic_smaa_t2x})
		assert(supported(selected));
	assert(temporal(mode::smaa_t2x) && temporal(mode::filmic_smaa_t2x) && !temporal(mode::smaa));
	assert(needs_history(mode::smaa_t2x, 16) && !needs_history(mode::smaa, 16) && needs_history(mode::filmic_smaa, 20));
	// Runtime None/FXAA -> SMAA and SMAA -> T2x/Filmic transitions have
	// different missing resources. Neither may invoke an unallocated target.
	assert(needs_temporal_input(mode::smaa_t2x) && needs_temporal_input(mode::filmic_smaa) &&
		needs_temporal_input(mode::filmic_smaa_t2x));
	assert(!needs_temporal_input(mode::none) && !needs_temporal_input(mode::fxaa) &&
		!needs_temporal_input(mode::smaa) && !needs_temporal_input(mode::fxaa_internal));
	assert(!supported(mode::fxaa_internal));
	assert(!supported(static_cast<mode>(0xffff)));
	std::array<bool, 65> ready{};
	const auto available = [&](std::uint32_t id) { return ready[id]; };
	for (const auto selected : {mode::smaa, mode::smaa_t2x, mode::filmic_smaa, mode::filmic_smaa_t2x})
		assert(transition_fallback(selected, available) == mode::fxaa);
	assert(transition_fallback(mode::none, available) == mode::none);
	assert(transition_fallback(mode::fxaa, available) == mode::fxaa);
	ready[14] = true;
	assert(transition_fallback(mode::smaa, available) == mode::fxaa);
	ready[15] = true;
	assert(transition_fallback(mode::smaa, available) == mode::smaa);
	assert(transition_fallback(mode::smaa_t2x, available) == mode::smaa);
	ready[64] = true;
	assert(transition_fallback(mode::smaa_t2x, available) == mode::smaa);
	for (const auto id : history_targets) ready[id] = true;
	for (const auto selected : {mode::smaa, mode::smaa_t2x, mode::filmic_smaa, mode::filmic_smaa_t2x})
		assert(transition_fallback(selected, available) == selected);
	for (const auto id : history_targets)
	{
		ready[id] = false;
		for (const auto selected : {mode::smaa_t2x, mode::filmic_smaa, mode::filmic_smaa_t2x})
			assert(transition_fallback(selected, available) == (needs_history(selected, id) ? mode::smaa : selected));
		ready[id] = true;
	}
}
