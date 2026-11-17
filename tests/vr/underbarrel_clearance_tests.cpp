#include "component/vr/gameplay/underbarrel_clearance.hpp"
#include <iostream>

int main()
{
	using namespace vr::gameplay::weapons::underbarrel;
	using namespace vr::gameplay::hands;
	unsigned failures{};
	unsigned checks{};
	const auto check = [&](bool value) { ++checks; if (!value) { ++failures; std::cerr << "failed check " << checks << '\n'; } };
	// Measured H1 M4 attachment envelopes: the case is authored far forward
	// at the animation's reload start, while physics owns a seated case.
	std::array<game::GfxPackedVertex, 3> tube_vertices{}, case_vertices{};
	tube_vertices[0].xyz[0] = .59234905f;
	tube_vertices[1].xyz[0] = 11.54023075f;
	tube_vertices[2].xyz[0] = 4.f;
	case_vertices[0].xyz[0] = 15.19522762f;
	case_vertices[1].xyz[0] = 16.41410446f;
	case_vertices[2].xyz[0] = 16.f;
	game::Face face{0, 1, 2};
	game::XRigidVertList tube_group{3 * 64, 3, 0, 1}, case_group{7 * 64, 3, 0, 1};
	std::array<game::XSurface, 2> surfaces{};
	for (auto& surface : surfaces)
	{
		surface.flags = 2; surface.vertCount = 3; surface.triCount = 1;
		surface.rigidVertListCount = 1; surface.triIndices = &face;
	}
	surfaces[0].verts0.packedVerts0 = tube_vertices.data(); surfaces[0].rigidVertLists = &tube_group;
	surfaces[1].verts0.packedVerts0 = case_vertices.data(); surfaces[1].rigidVertLists = &case_group;
	game::XModel model{};
	model.numBones = 8; model.numLods = 1;
	model.lodInfo[0].numsurfs = 2; model.lodInfo[0].surfs = surfaces.data();
	check(measure_case_clearance(&model, 3, 7, {}) == 0);
	const anchor rest{{16.51785278f, .059015f, -1.26197994f}, {0, 0, 0, 1}};
	const auto rebase = pose_math::compose(anchor{h1_m203_seated_round, rest.rotation}, pose_math::inverse(rest));
	const float distance = measure_case_clearance(&model, 3, 7, rebase);
	std::cout << "measured clearance=" << distance << '\n';
	check(std::abs(distance - 1.103903f) < .0001f);
	const identity id{{18, 1}, 19, kind::m203};
	auto ammo = import_native(id, 1, 9);
	const auto shot = plan(ammo, {operation::shot, id, ammo.revision, vr::hand::right, vr::hand::left, true});
	check(bool(shot)); ammo = shot.next;
	check(!case_unsupported(ammo, (distance - .001f) / 39.3701f, 39.3701f, distance));
	check(case_unsupported(ammo, (distance + .001f) / 39.3701f, 39.3701f, distance));
	check(case_vertices[1].xyz[0] == 16.41410446f);
	case_group.boneOffset = 6 * 64;
	check(measure_case_clearance(&model, 3, 7, rebase) == 0);
	check(measure_case_clearance(nullptr, 3, 7, rebase) == 0);
	std::cout << "MWR_M203_CLEARANCE_FAILURES=" << failures << '\n';
	return failures ? 1 : 0;
}
