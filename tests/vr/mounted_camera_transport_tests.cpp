#include "component/vr/mounted_camera_transport.hpp"
#include "component/vr/camera_rig.hpp"
#include <cassert>
#include <iostream>

int main()
{
	using namespace vr;
	const auto near=[](pose_filter::vec a,pose_filter::vec b){return pose_filter::length(pose_filter::sub(a,b))<.001f;};
	mounted_camera_transport::seat_anchor seat;
	pose_filter::pose carrier{{100,200,300}};pose_filter::vec eye{};
	assert(seat.update(962,123,100,carrier,{110,200,360},eye) && near(eye,{110,200,360}));
	const auto epoch=seat.revision();
	carrier.position={1100,2200,3300};
	// Aiming may change the native gun camera; the frozen carrier-local seat does not change.
	assert(seat.update(962,123,200,carrier,{1130,2200,3380},eye) && near(eye,{1110,2200,3360}));
	carrier.orientation={{{0,-1,0},{1,0,0},{0,0,1}}};
	assert(seat.update(962,123,300,carrier,{1100,2210,3360},eye) && near(eye,{1100,2210,3360}));
	assert(seat.revision()==epoch);
	// A frozen native view origin must not cancel an already acquired carrier.
	carrier.position={2100,3200,4300};
	assert(seat.update(962,123,350,carrier,{110,200,360},eye) && near(eye,{2100,3210,4360}));
	assert(seat.revision()==epoch);
	carrier.position={1100,2200,3300};
	assert(seat.update(963,123,400,carrier,{1100,2220,3350},eye) && near(eye,{1100,2220,3350}));
	assert(seat.revision()!=epoch);
	const auto second=seat.revision();
	assert(seat.update(963,456,500,carrier,{1100,2230,3340},eye) && near(eye,{1100,2230,3340}));
	assert(seat.revision()!=second);
	assert(seat.update(963,456,10,carrier,{1100,2240,3330},eye) && near(eye,{1100,2240,3330}));
	seat.reset();assert(seat.update(963,456,20,carrier,{1100,2250,3320},eye) && near(eye,{1100,2250,3320}));
	assert(!seat.update(964,456,30,carrier,{0,0,0},eye));
	assert(seat.update(963,456,40,carrier,{1100,2260,3310},eye) && near(eye,{1100,2260,3310}));
	// The same helicopter transitions from passenger boarding to its gun seat.
	assert(seat.update(963|(1ull<<33),456,50,carrier,{1100,2270,3370},eye) && near(eye,{1100,2270,3370}));

	// Heat: body/eye orbit the pintle when hands aim. The base, not that body,
	// supplies the carrier; root aim rotations must not rotate the seat either.
	mounted_camera_transport::seat_anchor ground;
	pose_filter::pose base{{-955.5f,348.7f,469.9f}};
	const pose_filter::vec seated{-941.5f,374.f,488.4f};
	assert(ground.update(553,789,100,mounted_camera_transport::translation_frame(base),seated,eye));
	for(float angle:{-31.f,31.f,0.f})
	{
		const float a=angle/57.295779513f,c=std::cos(a),s=std::sin(a);
		base.orientation={{{c,-s,0},{s,c,0},{0,0,1}}};
		assert(ground.update(553,789,200,mounted_camera_transport::translation_frame(base),
			{-939.f+30*s,378.4f,448.f},eye) && near(eye,seated));
	}
	base.position[0]+=10;
	assert(ground.update(553,789,300,mounted_camera_transport::translation_frame(base),
		{-939,378,448},eye) && near(eye,{-931.5f,374,488.4f}));

	// Existing camera composition must carry helicopter yaw while preserving free head motion.
	game_view::camera_rig rig;
	game_view::camera_request request{{game_view::camera_owner::script,game_view::head_rotation::free,
		game_view::head_translation::tracked,game_view::script_rotation::additive,game_view::script_axes::yaw,
		game_view::rotation_source::tag},42};
	game_view::camera_input input{pose_filter::identity,pose_filter::identity,{},0,0,0,100,1};
	game_view::scripted_rotation_reference reference{1,pose_filter::identity};
	auto view=rig.compose(input,request,&reference);assert(near(view.axis[0],{1,0,0}));
	reference.axis=pose_filter::transpose(carrier.orientation);input.time=200;
	view=rig.compose(input,request,&reference);assert(near(view.axis[0],{0,1,0}));
	input.head_axis=reference.axis;input.head_heading=90;input.time=300;
	view=rig.compose(input,request,&reference);assert(near(view.axis[0],{-1,0,0}));
	// Ground minigun and vehicle Mk19 both seat an off-centre physical head
	// once, then retain full translation and rotation. Passengers stay native.
	for(bool vehicle:{false,true})
	{
		game_view::camera_rig centered;
		auto mount=request;mount.epoch=100;mount.position_epoch=mount.epoch;
		if(!vehicle){mount.policy.script=game_view::script_rotation::ignore;mount.policy.source=game_view::rotation_source::view;}
		game_view::camera_input tracked{pose_filter::identity,pose_filter::identity,{.6f,-.4f,.2f},0,0,0,100,1};
		reference={1,pose_filter::identity};
		auto seated_view=centered.compose(tracked,mount,&reference);
		assert(near(seated_view.head_offset,{}));
		tracked.head_meters[1]+=.1f;tracked.head_axis={{{0,1,0},{-1,0,0},{0,0,1}}};tracked.head_heading=90;tracked.time=200;
		seated_view=centered.compose(tracked,mount,&reference);
		assert(near(seated_view.head_offset,{0,.1f,0}) && near(seated_view.axis[0],{0,1,0}));
		tracked.head_meters[1]-=.1f;tracked.time=300;
		assert(near(centered.compose(tracked,mount,&reference).head_offset,{}));
		mount.epoch=101;mount.position_epoch=101;tracked.head_meters={-.7f,.3f,-.1f};tracked.time=400;
		assert(near(centered.compose(tracked,mount,&reference).head_offset,{}));
		tracked.reference=2;tracked.head_meters={.2f,.1f,.5f};tracked.time=500;
		assert(near(centered.compose(tracked,mount,&reference).head_offset,{}));
		const game_view::camera_request ordinary;
		assert(near(centered.compose(tracked,ordinary).head_offset,tracked.head_meters));
		game_view::camera_rig passenger;mount.position_epoch=0;
		assert(near(passenger.compose(tracked,mount,&reference).head_offset,tracked.head_meters));
	}
	std::cout<<"MOUNTED_CAMERA_TRANSPORT_OK\n";
}
