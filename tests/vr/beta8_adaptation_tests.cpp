#include "controller_orientation_tests.hpp"
#include "holster_feedback_tests.hpp"
#include <iostream>

int main()
{
	unsigned failures{};
	const auto check=[&](bool valid,const char* message)
	{
		if (!valid) {++failures;std::cerr<<message<<'\n';}
	};
	controller_orientation_tests(check);
	holster_feedback_tests::run(check);
	using namespace vr::controller_calibration;
	vr::controller_input::frame raw;
	raw.sequence=1;raw.focused=true;
	for (auto& pose:raw.aim) {pose.valid=true;pose.tracking.orientation={{{1,0,0},{0,1,0},{0,0,1}}};}
	calibration independently;
	const auto result=independently.apply_hands(raw,{{angles{10,20,30},angles{-10,-20,-30}}});
	check(result.aim[0].tracking.orientation==rotation({10,20,30}) &&
		result.aim[1].tracking.orientation==rotation({-10,-20,-30}),"Each physical hand uses its own angles");
	const auto invalid=independently.apply_hands(raw,{{angles{181,0,0},angles{0,20,0}}});
	check(!invalid.aim[0].valid && invalid.aim[1].valid,"Bad calibration invalidates only its own hand");
	std::cout<<"BETA8_CALIBRATION_HOLSTER_FAILURES="<<failures<<'\n';
	return failures?1:0;
}
