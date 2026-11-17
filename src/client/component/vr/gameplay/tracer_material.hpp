#pragma once
#include "game/assets.hpp"
#include "component/d3d11.hpp"

namespace vr::gameplay::weapons::tracer_material
{
	// Clone resident H1 providers; colored shots must not tint the shared orange image.
	game::Material* request(game::Material* source);
	void prepare(const d3d11::present_event& event);
	void clear_after_drain();
	std::string status();
}
