#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"
#include "game/ui_scripting/execution.hpp"

#include "command.hpp"
#include "console.hpp"
#include "camera.hpp"
#include "gui/gui.hpp"
#include "vr/gameplay/ladder_runtime.hpp"
#include "vr/gameplay/scripted_motion.hpp"
#include "vr/gameplay/javelin_screen.hpp"
#include "vr/gameplay/physical_stance.hpp"
#include "vr/gameplay/fixed_sniper.hpp"
#include "vr/gameplay/barrett_gun.hpp"
#include "vr/gameplay/view_aimed_weapon.hpp"
#include "vr/engine_stereo_renderer.hpp"
#include "vr/head_pose_bridge.hpp"
#include "vr/mounted_camera_transport.hpp"
#include "vr/h2/entrypoints.hpp"
#include "vr/gameplay/hands/native_rig.hpp"

#include <utils/hook.hpp>
#include <utils/concurrency.hpp>
#include <utils/native_memory.hpp>

namespace camera
{
	namespace
	{
		utils::hook::detour sub_140781090_hook;

		game::dvar_t* cl_free_move_scale = nullptr;
		game::dvar_t** cg_paused = nullptr;
		
		using cb_type = std::optional<std::function<void()>>;
		utils::concurrency::container<cb_type> callback;

		float angle_normalize(const float angle)
		{
			const auto value = ((angle * 0.0027777778f) - std::floorf(angle * 0.0027777778f)) * 360.f;
			auto result = value - 360.f;

			if ((value - 360.f) < 0.f)
			{
				result = value;
			}

			return result;
		}

		void vec_cross(float* a1, float* a2, float* a3)
		{
			a3[0] = (a2[2] * a1[1]) - (a2[1] * a1[2]);
			a3[1] = (a1[2] * a2[0]) - (a2[2] * a1[0]);
			a3[2] = (a2[1] * a1[0]) - (a1[1] * a2[0]);
		}

		float camera_origin[3]{};

		auto using_camera_override = false;
		auto using_camera_angles_override = false;

		float camera_origin_override[3]{};
		float camera_angles_override[3]{};

		vr::mounted_camera_transport::seat_anchor mounted_view;
		struct mounted_diagnostic
		{
			bool active{},vehicle{};unsigned entity{};
			std::array<float,3> carrier{},eye{};float yaw{};
			std::uint64_t followed{},misses{};
		} mounted_probe;
		bool mounted_hand_aim()
		{
			if (!vr::gameplay::view_aimed::mounted() || vr::gameplay::view_aimed::active() || vr::gameplay::barrett_gun::epoch()) return false;
			const auto* aim = game::Dvar_FindVar("vr_turretHandAim");
			return aim && aim->current.enabled;
		}
		bool rendered_carrier(unsigned entity,vr::pose_filter::pose& carrier,
			vr::game_view::scripted_rotation_reference& rotation,std::uintptr_t& identity,bool require_vehicle_model=false)
		{
			if(!entity || entity>=3998)return false;
			std::uint16_t handle{};
			// MWR client cposes: CG_CalcTurretView 0x140185D60 uses this array,
			// 0x200 stride and +0x1B2 DObj handle. Pose origin/angles are +1C/+28.
			const auto* pose=reinterpret_cast<const std::byte*>(0x14215D500ull+std::uintptr_t(entity)*0x200);
			std::array<float,6> transform{};
			if(!utils::native_memory::read_bytes(&handle,pose+0x1b2,2) || !handle || handle>=4032 ||
				!utils::native_memory::read_bytes(transform.data(),pose+0x1c,sizeof(transform)))return false;
			const auto* object=vr::h2::sp::client_entity_dobj(handle,0);
			if(!object)return false;
			if(require_vehicle_model)
			{
				vr::gameplay::hands::native_object description{};
				game::XModel* model{};const char* name{};std::array<char,8> prefix{};
				if(!utils::native_memory::read_bytes(&description,object,sizeof(description)) || !description.model_count ||
					!description.models || !utils::native_memory::read_bytes(&model,description.models,sizeof(model)) || !model ||
					!utils::native_memory::read_bytes(&name,&model->name,sizeof(name)) || !name ||
					!utils::native_memory::read_bytes(prefix.data(),name,8) || std::string_view(prefix.data(),8)!="vehicle_")return false;
			}
			for(float x:transform)if(!std::isfinite(x) || std::abs(x)>100000.f)return false;
			std::array<std::array<float,3>,3> axis{};
			game::AnglesToAxis(transform.data()+3,reinterpret_cast<float(*)[3]>(axis.data()));
			carrier={{transform[0],transform[1],transform[2]},vr::pose_filter::transpose(axis)};
			if(!vr::pose_filter::valid(carrier))return false;
			identity=reinterpret_cast<std::uintptr_t>(object);rotation.axis=axis;return true;
		}

		void paused_free_move()
		{
			// MWR: cg_paused has not been rebound; free-move stays disabled.
			if (cg_paused == nullptr || (*cg_paused)->current.integer != 2)
			{
				return;
			}

			constexpr auto local_client_num = 0;
			const auto ps = game::CG_GetPredictedPlayerState(local_client_num);
			const auto cmd_number = game::CL_GetCurrentCmdNumber(local_client_num);

			game::usercmd_s cmd{};
			if (!game::CL_GetUserCmd(local_client_num, cmd_number, &cmd))
			{
				return;
			}

			callback.access([](cb_type& cb)
			{
				if (cb.has_value())
				{
					auto& fn = cb.value();
					fn();
				}
			});

			if (using_camera_angles_override)
			{
				game::cgs->refdefViewAngles[0] = camera_angles_override[0];
				game::cgs->refdefViewAngles[1] = camera_angles_override[1];
				game::cgs->refdefViewAngles[2] = camera_angles_override[2];
			}
			else
			{
				game::cgs->refdefViewAngles[0] = angle_normalize((cmd.angles[0] * 0.000021457672f) + ps->delta_angles[0]);
				game::cgs->refdefViewAngles[1] = angle_normalize((cmd.angles[1] * 0.000021457672f) + ps->delta_angles[1]);
				game::cgs->refdefViewAngles[2] = 0;
			}

			const auto game_time = game::CG_GetGameTime(local_client_num);

			static auto saved_game_time = 0;
			static auto saved_ms = 0;

			if (saved_game_time != game_time)
			{
				saved_game_time = game_time;
				saved_ms = game::Sys_Milliseconds();
			}

			const auto now = game::Sys_Milliseconds();
			const auto diff = now - saved_ms;
			saved_ms = game::Sys_Milliseconds();

			auto scale = 1.f;
			if ((cmd.buttons & 0x6) != 0)
			{
				scale = 10.f;
			}

			scale *= cl_free_move_scale->current.value;

			game::AnglesToAxis(game::cgs->refdefViewAngles, game::refdef->axis);

			float v1[3] = {0.f, 0.f, 1.f};
			float v2[3] = {game::refdef->axis[0][0], game::refdef->axis[0][1], game::refdef->axis[0][2]};
			float v3[3]{};

			vec_cross(v1, v2, v3);
			vec_cross(v3, v1, v2);

			if (cmd.forwardmove)
			{
				ps->origin[0] += scale * (game::refdef->axis[0][0] * (static_cast<float>(cmd.forwardmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
				ps->origin[1] += scale * (game::refdef->axis[0][1] * (static_cast<float>(cmd.forwardmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
				ps->origin[2] += scale * (game::refdef->axis[0][2] * (static_cast<float>(cmd.forwardmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
			}

			if (cmd.rightmove)
			{
				ps->origin[0] += scale * (v3[0] * (static_cast<float>(-1 * cmd.rightmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
				ps->origin[1] += scale * (v3[1] * (static_cast<float>(-1 * cmd.rightmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
				ps->origin[2] += scale * (v3[2] * (static_cast<float>(-1 * cmd.rightmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
			}

			if ((cmd.buttons & 0x1) != 0)
			{
				ps->origin[2] += scale * 10.f;
			}

			if ((cmd.buttons & 0x800) != 0)
			{
				ps->origin[2] -= scale * 10.f;
			}

			if (using_camera_override)
			{
				game::refdef->org[0] = camera_origin_override[0];
				game::refdef->org[1] = camera_origin_override[1];
				game::refdef->org[2] = camera_origin_override[2];
			}
			else
			{
				game::refdef->org[0] = ps->origin[0];
				game::refdef->org[1] = ps->origin[1];
				game::refdef->org[2] = ps->origin[2] + ps->viewHeightCurrent;
			}

			camera_origin[0] = game::refdef->org[0];
			camera_origin[1] = game::refdef->org[1];
			camera_origin[2] = game::refdef->org[2];
		}

		// MWR: there is no rebound equivalent of H2's set_viewpos_now call
		// (0x1403ACB2D). Hook CG_DrawActiveFrame's CG_CalcViewValues call
		// (0x140186B9E -> 0x140189AB0) instead: the native view is final when it
		// returns and every later consumer (FX camera publication at 0x140238840,
		// scene owner 0x1401A32F0) reads the HMD-applied refdef. The H2 campaign
		// sequence, fixed sniper, mounted, ladder, notebook, javelin and laser
		// camera adapters are MW2-specific and are not invoked here.
		constexpr std::uintptr_t calc_view_values_call_site = 0x140186B9E;
		constexpr std::uintptr_t calc_view_values = 0x140189AB0;
		constexpr std::uintptr_t camera_lod_helper = 0x140593AA0;
		constexpr std::uintptr_t camera_primary_globals = 0x14F79D2A0;

		void set_viewpos_now_stub(const int local_client_num)
		{
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			auto* const a1 = reinterpret_cast<void*>(static_cast<std::uintptr_t>(local_client_num));
			vr::engine_stereo_renderer::observe_camera_state(
				vr::engine_stereo_renderer::camera_observation_source::set_viewpos_now,
				vr::engine_stereo_renderer::camera_observation_stage::enter,
				a1, nullptr, 0.0f, caller);
			utils::hook::invoke<void>(calc_view_values, local_client_num);
			paused_free_move();
			const auto* const ps = game::CG_GetPredictedPlayerState(local_client_num);
			// Physical ladder: preview the held-hand pull between server samples.
			if (ps)
				vr::gameplay::ladders::apply_camera_origin(game::refdef->org, ps->viewHeightCurrent,
					vr::gameplay::ladders::owned_carrier(), ps->commandTime);
			// Story rigs that throw the body around (rope descents, crashes): the
			// authored eye with no room-scale offset, centring the player.
			vr::game_view::camera_request request{};
			vr::game_view::scripted_rotation_reference carrier_rotation{};
			// Charlie Don't Surf rides a linked UH-60 with an ordinary weapon;
			// it never enters the Mk19's weaponless vehicle-turret state.
			int linked=vr::gameplay::scripted_motion::linked_parent();
			vr::pose_filter::pose linked_pose;std::uintptr_t linked_identity{};
			bool linked_vehicle{};
			// H1 armada: player -> worldbody_h1_usmc_desert -> Black Hawk.
			// Do not mistake that intermediate animated body for the carrier.
			for(unsigned depth=0;depth<8 && linked>0 && linked!=vr::gameplay::ladders::owned_carrier();++depth)
			{
				if(rendered_carrier(unsigned(linked),linked_pose,carrier_rotation,linked_identity,true))
				{linked_vehicle=true;break;}
				linked=vr::gameplay::scripted_motion::linked_parent(linked);
			}
			const bool gunship = vr::gameplay::view_aimed::gunship();
			static bool was_gunship{};
			static std::uint64_t gunship_epoch{};
			if (gunship && !was_gunship) ++gunship_epoch;
			was_gunship = gunship;
			if (vr::gameplay::scripted_motion::active())
				request.policy.translation = vr::game_view::head_translation::fixed;
			// Fixed sniper (flat scope view): the native turret camera, no head pose
			// (original Overlord thermal_scope camera request).
			if (const auto sniper = vr::gameplay::fixed_sniper::current(ps).epoch)
				request = {vr::game_view::camera_profiles::fixed_scope, sniper | (1ull << 63)};
			// AC-130: the flat native frame is the gunship monitor, aimed by the right
			// stick. Composing the HMD rotation into it made the head steer the
			// picture (and the crosshair) away from where the guns point.
			else if (gunship)
				request = {vr::game_view::camera_profiles::fixed_scope, gunship_epoch | (1ull << 59)};
			// Stinger on the Javelin display steers the native view with its tube:
			// hold the entry heading as for the Barrett.
			else if (const auto stinger = vr::gameplay::weapons::javelin_screen::head_aim() ? 0 : vr::gameplay::weapons::javelin_screen::stinger_view_epoch())
				request = {{vr::game_view::camera_owner::script, vr::game_view::head_rotation::free,
					vr::game_view::head_translation::tracked}, stinger | (1ull << 60)};
			// Held Barrett: the barrel steers the native angles, so the camera holds
			// its entry heading and the head turns and moves freely.
			else if (const auto barrett = vr::gameplay::barrett_gun::epoch())
			{
				vr::gameplay::barrett_gun::observe_native_eye(game::refdef->org);
				request = {{vr::game_view::camera_owner::script, vr::game_view::head_rotation::free,
					vr::game_view::head_translation::tracked}, barrett | (1ull << 61)};
			}
			// Mounted gun swung by the hands (vr_turretHandAim, village_defend minigun,
			// airlift Mk19): the hands set the native angles, and the native eye sits
			// on the gun's aim chain (tag_player), so the whole view rotated and bobbed
			// with every hand movement on top of the player's own turn (user report).
			// Keep the seat in the rendered carrier frame. A player-relative offset
			// followed translation but left the view heading behind on helicopter turns.
			else if (mounted_hand_aim() || linked_vehicle)
			{
				const bool hand_aim=mounted_hand_aim();
				const bool vehicle=linked_vehicle || (ps && (ps->e_flags&0x100000));
				vr::pose_filter::pose carrier;
				std::uintptr_t identity{};
				std::uint16_t entity{};
				if(ps)std::memcpy(&entity,reinterpret_cast<const std::byte*>(ps)+0x1e,2);
				if(linked_vehicle){entity=static_cast<std::uint16_t>(linked);carrier=linked_pose;identity=linked_identity;}
				const bool carrier_ready=linked_vehicle || rendered_carrier(entity,carrier,carrier_rotation,identity);
				if(!vehicle)
				{
					// Heat's native player origin orbits the minigun as it aims.
					// Anchor to the rendered turret base instead of that moving body.
					carrier=vr::mounted_camera_transport::translation_frame(carrier);
					carrier_rotation={};
				}
				const std::array<float,3> native_eye{game::refdef->org[0],game::refdef->org[1],game::refdef->org[2]};
				std::array<float,3> eye{};
				// Boarding as a passenger and taking the gun may share one DObj,
				// but their authored eye anchors are different seats.
				if(carrier_ready && mounted_view.update(std::uint64_t(entity)|(vehicle?1ull<<32:0)|(hand_aim?1ull<<33:0),identity,
					ps->commandTime,carrier,native_eye,eye))
				{
					if(hand_aim)std::copy(eye.begin(),eye.end(),game::refdef->org);
					if(vehicle)carrier_rotation.source=mounted_view.revision();
					++mounted_probe.followed;
				}
				else {mounted_view.reset();++mounted_probe.misses;} // Missing carrier: use the current native eye.
				mounted_probe.active=true;mounted_probe.vehicle=vehicle;mounted_probe.entity=entity;
				mounted_probe.carrier=carrier.position;mounted_probe.eye={game::refdef->org[0],game::refdef->org[1],game::refdef->org[2]};
				mounted_probe.yaw=vehicle?std::atan2(carrier_rotation.axis[0][1],carrier_rotation.axis[0][0])*57.295779513f:0.f;
				request = {{vr::game_view::camera_owner::script, vr::game_view::head_rotation::free,
					vr::game_view::head_translation::tracked}, mounted_view.revision() | (1ull << 58)};
				// Seat the current physical head at the authored eye once on gun
				// entry. Keep subsequent head motion 1:1 in the shared hand frame.
				if(hand_aim && carrier_ready)request.position_epoch=request.epoch;
				if(carrier_rotation.source)
				{
					request.policy.script=vr::game_view::script_rotation::additive;
					request.policy.source=vr::game_view::rotation_source::tag;
				}
			}
			if (!mounted_hand_aim() && !linked_vehicle) {mounted_view.reset();mounted_probe.active=false;}
			// Real crouch/prone already lowers the tracked head: cancel the native
			// eye-height drop up to that much so the view is not lowered twice.
			if (ps && request.policy == vr::game_view::camera_profiles::gameplay && !request.epoch)
			{
				const auto head = vr::head_pose_bridge::get_status();
				game::refdef->org[2] += vr::gameplay::physical_stance::eye_compensation_units(ps->viewHeightCurrent, head.world_scale);
			}
			if (ps && vr::head_pose_bridge::apply_camera(game::refdef->org, game::refdef->axis,
				ps->commandTime, game::cgs->refdefViewAngles, request,carrier_rotation.source?&carrier_rotation:nullptr))
			{
				camera_origin[0] = game::refdef->org[0];
				camera_origin[1] = game::refdef->org[1];
				camera_origin[2] = game::refdef->org[2];
				// Javelin CLU at the eye: only the scene/HUD camera moves to the
				// launcher (tracking and hand space stay HMD-owned), as on H2.
				// The Stinger's held heading is ours, not a script camera: keep the display.
				vr::gameplay::weapons::javelin_screen::apply_camera(game::refdef->org, game::refdef->axis,
					!request.policy.owns_rotation() || vr::gameplay::weapons::javelin_screen::stinger_view_epoch(),
					&game::refdef->fovX);
				// MWR: CG_CalcViewValues copies the view origin into refdef.viewOffset
				// (+0x58, the origin viewmodel DObj matrices and tag queries are relative
				// to) before this hook applies the HMD pose. H2 recomputed it after the
				// pose (0x1403B07C0), so hands and the scene shared the tracked head.
				auto* const view_offset = reinterpret_cast<float*>(reinterpret_cast<std::byte*>(&game::refdef->org[0]) - 0x20 + 0x58);
				for (int i = 0; i < 3; ++i) view_offset[i] = game::refdef->org[i];
			}
			vr::engine_stereo_renderer::observe_camera_state(
				vr::engine_stereo_renderer::camera_observation_source::set_viewpos_now,
				vr::engine_stereo_renderer::camera_observation_stage::leave,
				a1, nullptr, 0.0f, caller);
		}

		void sub_140781090_stub(void* a1, void* a2, float a3)
		{
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			vr::engine_stereo_renderer::observe_camera_state(
				vr::engine_stereo_renderer::camera_observation_source::camera_helper,
				vr::engine_stereo_renderer::camera_observation_stage::before_original,
				a1, a2, a3, caller);
			sub_140781090_hook.invoke<void>(a1, a2, a3);
			vr::engine_stereo_renderer::observe_camera_state(
				vr::engine_stereo_renderer::camera_observation_source::camera_helper,
				vr::engine_stereo_renderer::camera_observation_stage::after_original,
				a1, a2, a3, caller);

			if (is_free_move_enabled() || vr::head_pose_bridge::camera_was_applied())
			{
				const auto lod_origin = reinterpret_cast<float*>(camera_primary_globals);
				lod_origin[0] = game::refdef->org[0];
				lod_origin[1] = game::refdef->org[1];
				lod_origin[2] = game::refdef->org[2];
				vr::engine_stereo_renderer::observe_camera_state(
					vr::engine_stereo_renderer::camera_observation_source::camera_helper,
					vr::engine_stereo_renderer::camera_observation_stage::after_mod,
					a1, a2, a3, caller);
			}
		}
	}

	void enable_free_move()
	{
		if (cg_paused && *cg_paused) (*cg_paused)->current.integer = 2;
	}

	void disable_free_move()
	{
		if (cg_paused && *cg_paused) (*cg_paused)->current.integer = 0;
	}

	bool is_free_move_enabled()
	{
		return cg_paused && *cg_paused && (*cg_paused)->current.integer == 2;
	}

	void set_camera_position(const float x, const float y, const float z)
	{
		camera_origin_override[0] = x;
		camera_origin_override[1] = y;
		camera_origin_override[2] = z;
	}

	void set_camera_angles(const float x, const float y, const float z)
	{
		camera_angles_override[0] = x;
		camera_angles_override[1] = y;
		camera_angles_override[2] = z;
	}

	void set_using_origin_override(bool value)
	{
		using_camera_override = value;
	}

	void set_using_angles_override(bool value)
	{
		using_camera_angles_override = value;
	}

	ui_scripting::table get_camera_position()
	{
		ui_scripting::table res;
		res["x"] = camera_origin[0];
		res["y"] = camera_origin[1];
		res["z"] = camera_origin[2];
		return res;
	}

	void set_callback(const ui_scripting::script_value& callback_)
	{
		callback.access([=](cb_type& cb)
		{
			cb = [=]()
			{
				try
				{
					const auto state = *game::hks::lua_state;
					const ui_scripting::table globals = state->globals.v.table;
					const auto pcall = globals["pcall"];
					const auto results = pcall(callback_);

					if (!results[0].as<bool>())
					{
						const auto error = results[1].as<std::string>();
						console::error("Error executing camera callback: %s\n", error.data());
					}
				}
				catch (const std::exception& e)
				{
					console::error("Error executing camera callback: %s\n", e.what());
				}
			};
		});
	}

	void clear_lua()
	{
		callback.access([=](cb_type& cb)
		{
			cb.reset();
		});
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			cl_free_move_scale = dvars::register_float("cl_freemoveScale", 1.f, 0.0001f, 100.f,
				game::DVAR_FLAG_SAVED, "Scale how fast you move in cl_freemove mode");

			utils::hook::call(calc_view_values_call_site, set_viewpos_now_stub);
			sub_140781090_hook.create(camera_lod_helper, sub_140781090_stub);
			command::add("vr_mounted_camera_status",[] {
				console::info("[VR mounted camera] active=%d vehicle=%d entity=%u revision=%llu followed=%llu misses=%llu carrier=(%.1f,%.1f,%.1f) yaw=%.2f eye=(%.1f,%.1f,%.1f)\n",
					mounted_probe.active,mounted_probe.vehicle,mounted_probe.entity,mounted_view.revision(),
					mounted_probe.followed,mounted_probe.misses,mounted_probe.carrier[0],mounted_probe.carrier[1],mounted_probe.carrier[2],
					mounted_probe.yaw,mounted_probe.eye[0],mounted_probe.eye[1],mounted_probe.eye[2]);
				vr::head_pose_bridge::spatial_frame frame;
				if(vr::head_pose_bridge::get_spatial_frame(frame))
					console::info("[VR mounted head] eye=(%.3f,%.3f,%.3f) yaw=%.3f pitch=%.3f\n",
						frame.head_position[0],frame.head_position[1],frame.head_position[2],
						std::atan2(frame.head_yaw_axis[0][1],frame.head_yaw_axis[0][0])*57.295779513f,
						vr::head_pose_bridge::get_status().output_pitch_degrees);
			});
		}
	};
}

REGISTER_COMPONENT(camera::component)
