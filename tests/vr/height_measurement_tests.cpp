#include "component/vr/height_measurement.hpp"
#include <cassert>
#include <limits>

using namespace vr::player_height::measurement;
int main()
{
	const std::array<float,3> head{0,0,1.65f},left{0,.8f,1.4f},right{0,-.8f,1.4f};
	assert(tpose(head,left,right));
	// The same physical pose remains valid after a 90-degree turn.
	assert(tpose(head,{-.8f,0,1.4f},{.8f,0,1.4f}));
	assert(!tpose(head,{.6f,.2f,1.4f},{.6f,-.2f,1.4f}));
	assert(!tpose(head,left,{0,-.8f,1.1f}));
	assert(!tpose(head,{0,.8f,std::numeric_limits<float>::quiet_NaN()},right));
	hold capture;
	const sample value{1.65f,1.6f,.25f};
	assert(capture.update(true,true,state::holding,value,.1f)==state::release);
	capture.update(false,false,state::holding,value,0);
	for(int i=0;i<10;++i)assert(capture.update(true,true,state::holding,value,.1f)==state::holding);
	assert(capture.progress()>.49f && capture.progress()<.51f);
	// Releasing either trigger or losing pose discards the partial capture.
	assert(capture.update(true,false,state::holding,value,.1f)==state::pose);
	assert(capture.progress()==0);
	capture.update(true,true,state::holding,value,.1f);
	assert(capture.update(true,true,state::floor,value,.1f)==state::floor);
	assert(capture.progress()==0);
	capture.update(true,true,state::holding,value,.1f);
	assert(capture.update(true,true,state::holding,value,2.f)==state::tracking);
	assert(capture.progress()==0);
	for(int i=0;i<20;++i)capture.update(true,true,state::holding,value,.1f);
	assert(capture.progress()==1.f);
	assert(std::abs(capture.average().eye-value.eye)<.001f);
	assert(std::abs(capture.average().span-value.span)<.001f);
	// New capture/focus epochs require another release before counting time.
	capture.reset();
	assert(capture.update(true,true,state::holding,value,.1f)==state::release);
	assert(capture.progress()==0);
}
