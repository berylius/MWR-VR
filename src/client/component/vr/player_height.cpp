#include <std_include.hpp>
#include "player_height.hpp"
#include "height_measurement.hpp"
#include "settings.hpp"
#include "controller_haptics.hpp"
#include "controller_input.hpp"
#include "head_pose_bridge.hpp"
#include "gameplay/weapon_holsters.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

// vr_playerHeight (cm) is entered in the launcher or measured in game: Options >
// VR "Measure" or a 3 s menu/recenter button hold (recenter fires at 1 s) arms
// it, then a T-pose with both triggers pulled measures height, shoulders and
// arm length over a continuous 2 s hold. The HMD's floor height comes from the OpenXR stage
// space or the OpenVR standing universe; runtimes without a floor keep the
// entered value.
namespace vr::player_height
{
	namespace
	{
		game::dvar_t* height{};
		std::atomic<float> last_measured_cm{NAN},last_eye_meters{NAN};
		std::atomic_uint64_t measurements{},rejected{};
		std::atomic<float> waist_below_body{NAN};
		game::dvar_t* arm{};
		game::dvar_t* arm_length{};
		bool fit_pending{};
		float fitted_length{};
		game::dvar_t* measured{}; // Successful measurements this session; the VR menu polls it.
		std::string last_arms{"not measured"};
		game::dvar_t *measurement_state{}, *measurement_progress{};
		measurement::hold capture;
		bool armed{};
		controller_input::clock::time_point previous_sample{};
		controller_input::clock::time_point page_seen{};
		std::uint64_t previous_reference{};
		void publish(measurement::state state)
		{
			if(measurement_state) measurement_state->current.integer=static_cast<int>(state);
			if(measurement_progress) measurement_progress->current.integer=int(std::round(capture.progress()*1000));
		}

		void apply()
		{
			if(arm_length && arm_length->current.value>0 && (fit_pending || fitted_length!=arm_length->current.value))
			{
				const float model=model_arm_units.load()/head_pose_bridge::get_status().world_scale;
				if(std::isfinite(model) && model>.2f)
				{
					const float scale=std::clamp(arm_length->current.value/model,min_arm_scale,max_arm_scale);
					command::execute(std::format("set vr_armScale {:.3f}",scale),true);
					fit_pending=false;
					fitted_length=arm_length->current.value;
				}
			}
			body_scale = scale_for(height ? height->current.value : reference_cm);
			arm_scale = arm ? arm->current.value : 1.f;
		}
		void pulse(bool accepted)
		{
			const auto frame = controller_input::latest();
			for (int hand = 0; hand < 2; ++hand)
				controller_haptics::request(hand, accepted ? controller_haptics::pulse{.30f, 160.f, .8f, frame.reference_generation, frame.sampled_at}
					: controller_haptics::pulse{.08f, 60.f, .35f, frame.reference_generation, frame.sampled_at});
		}
		void set_dvar(const char* name, float value)
		{
			command::execute(std::format("set {} {:.3f}", name, value), true);
		}
		// T-pose (arms straight out to the sides at shoulder height): the hands
		// give the shoulder height below the eyes, and the span gives the arm.
		// Shoulder half width follows height (joint centres, not the shoulder
		// edge); the arm is the rest of each half span.
		using tpose_sample=measurement::pose;
		std::optional<tpose_sample> tpose(const controller_input::frame& frame)
		{
			head_pose_bridge::tracking_reference reference{};
			if (!head_pose_bridge::get_tracking_reference(reference) || reference.generation != frame.reference_generation) return {};
			std::array<head_pose_bridge::vector3, 2> hands{};
			for (int hand = 0; hand < 2; ++hand)
				if (!controller_input::interaction_ready(frame.grip[hand]) ||
					!head_pose_bridge::tracking_position(reference, frame.grip[hand].tracking.position_meters, hands[hand])) return {};
			// Recenter-local metres: x forward, y left, z up.
			const auto& head = reference.head_position_meters;
			const float span = std::hypot(hands[0][0] - hands[1][0], hands[0][1] - hands[1][1]);
			const float drop = head[2] - (hands[0][2] + hands[1][2]) * .5f;
			const auto result=measurement::tpose(head,hands[0],hands[1]);
			last_arms = std::format("{}: span {:.2f} m, hands {:.2f} m below the eyes, level difference {:.2f} m",
				result ? "T-pose" : "not a T-pose", span, drop, std::abs(hands[0][2] - hands[1][2]));
			return result;
		}
		void apply_arms(const tpose_sample& pose, float cm)
		{
			const float half_width = std::clamp(shoulder_half_width_per_height * cm / 100.f, .10f, .35f);
			const float wrist_reach = pose.span * .5f - half_width - grip_to_wrist_m;
			set_dvar("vr_measuredArmLength",wrist_reach);
			set_dvar("vr_shoulderDown", std::clamp(pose.drop, .05f, .45f));
			set_dvar("vr_shoulderHalfWidth", half_width);
			const float model = model_arm_units.load() / head_pose_bridge::get_status().world_scale;
			std::string result = "arm model unknown (hands not drawn yet)";
			fit_pending=true;
			if (std::isfinite(model) && model > .2f && wrist_reach > .2f)
			{
				const float scale = std::clamp(wrist_reach / model, min_arm_scale, max_arm_scale);
				set_dvar("vr_armScale", scale);
				fit_pending=false;
				fitted_length=arm_length ? arm_length->current.value:0.f;
				result = std::format("arm {:.2f} m / model {:.2f} m -> vr_armScale {:.3f}", wrist_reach, model, scale);
			}
			last_arms += std::format("; shoulders {:.2f} m below the eyes, half width {:.2f} m, {}", pose.drop, half_width, result);
		}
		bool commit_measurement(float eye,const tpose_sample& pose)
		{
			const auto cm=measured_cm(eye);
			if(!cm)return false;
			++measurements;last_eye_meters=eye;last_measured_cm=*cm;
			command::execute(std::format("set {} {}",settings::player_height.name,int(*cm)),true);
			apply_arms(pose,*cm);
			if(measured)command::execute(std::format("set vr_heightMeasured {}",measurements.load()),true);
			apply();pulse(true);
			command::execute("writeconfig config.cfg",true);
			console::info("[VR height] measured %.0f cm (eye %.2f m above the floor); %s\n",*cm,eye,last_arms.c_str());
			return true;
		}
		bool measure()
		{
			const auto frame = controller_input::latest();
			const auto eye = floor_height();
			const auto cm = eye ? measured_cm(*eye) : std::nullopt;
			if (!cm)
			{
				++rejected;pulse(false);
				console::info(eye ? "[VR height] measurement rejected: eye %.2f m above the floor (stand upright)\n"
					: "[VR height] measurement unavailable: the VR runtime reports no floor height\n", eye.value_or(0.f));
				return false;
			}
			const auto pose = tpose(frame);
			if (!pose)
			{
				++rejected;pulse(false);
				console::info("[VR height] %s (hold both arms straight out to the sides)\n", last_arms.c_str());
				return false;
			}
			return commit_measurement(*eye,*pose);
		}
		// Armed by the menu button or a 3 s X hold: stand in a T-pose and pull
		// both triggers. Both must be released first, so the press that armed
		// it (or a held weapon trigger) never measures.
		void arm_measurement()
		{
			using namespace std::chrono_literals;
			const auto frame = controller_input::latest();
			armed=true;capture.reset();previous_sample={};previous_reference=0;
			page_seen=controller_input::clock::now();
			publish(measurement::state::release);
			for (int hand = 0; hand < 2; ++hand)
				controller_haptics::request(hand, {.06f, 90.f, .35f, frame.reference_generation, frame.sampled_at});
			console::info("[VR height] armed: T-pose, release then hold both triggers for 2 seconds\n");
		}
		void watch_triggers(const controller_input::frame& frame, controller_input::clock::time_point now)
		{
			if(!armed)return;
			// A level load or VM replacement may discard a page without menu_close.
			// Never finish a held capture after its visible guide has disappeared.
			if(now-page_seen>1s)
			{armed=false;capture.reset();publish(measurement::state::idle);return;}
			const auto& left = frame.trigger[0];
			const auto& right = frame.trigger[1];
			if(!frame.focused || now<frame.sampled_at || now-frame.sampled_at>150ms || !left.active || !right.active)
			{capture.reset();previous_sample={};publish(measurement::state::tracking);return;}
			if(frame.sampled_at==previous_sample)return;
			const float elapsed=previous_sample==controller_input::clock::time_point{} ? 0.f:
				std::chrono::duration<float>(frame.sampled_at-previous_sample).count();
			if(previous_reference && previous_reference!=frame.reference_generation)capture.reset();
			previous_sample=frame.sampled_at;previous_reference=frame.reference_generation;
			const auto eye=floor_height();
			const auto pose=tpose(frame);
			auto admission=measurement::state::holding;
			if(!controller_input::interaction_ready(frame.grip[0]) || !controller_input::interaction_ready(frame.grip[1]))admission=measurement::state::tracking;
			else if(!eye || !measured_cm(*eye))admission=measurement::state::floor;
			else if(!pose)admission=measurement::state::pose;
			const auto state=capture.update(left.down,right.down,admission,
				{eye.value_or(0.f),pose ? pose->span:0.f,pose ? pose->drop:0.f},elapsed);
			if(state==measurement::state::complete)
			{
				const auto value=capture.average();
				if(commit_measurement(value.eye,{value.span,value.drop}))armed=false;
			}
			publish(state);
		}
		// The menu/recenter button held for 3 s arms the measurement once per hold.
		// Watched here, not in the menu gesture: the 1 s recenter resets menu input state.
		void watch_hold()
		{
			using namespace std::chrono_literals;
			static controller_input::clock::time_point since{},seen{};
			static bool held{},fired{};
			const auto frame = controller_input::latest();
			const auto now = controller_input::clock::now();
			const auto& button = frame.menu_recenter;
			watch_triggers(frame,now);
			if (!frame.focused || now < frame.sampled_at || now - frame.sampled_at > 500ms)
			{
				if (held && now - seen > 500ms) held = fired = false;
				return;
			}
			if (!button.active) return;
			seen = now;
			if (!button.down) {held = fired = false;return;}
			if (!held) {held = true;fired = false;since = frame.sampled_at;}
			if (!fired && frame.sampled_at - since >= 3s)
			{fired=true;arm_measurement();command::execute("lui_open vr_height_measure",true);}
		}
		// Status witness: applied default waist holster depth below the body anchor, physical metres.
		void sample_layout()
		{
			head_pose_bridge::spatial_frame frame{};
			if (!head_pose_bridge::get_spatial_frame(frame) || !(frame.units_per_meter > 0)) return;
			if (const auto slots = gameplay::weapons::carry::locate_holsters(frame); slots.valid)
				waist_below_body = (head_pose_bridge::body_slots_frame(frame).head_position[2] - slots.centers[0][2]) / frame.units_per_meter;
		}
	}
	std::string status()
	{
		const auto eye = floor_height();
		// Applied waist holster depth below the body anchor, physical metres.
		return std::format("[VR height] vr_playerHeight={} body_scale={:.3f} floor_eye_m={} last_measured_cm={} measurements={} rejected={} waist_below_body_m={:.3f}\n"
			"[VR height] vr_armScale={:.3f} model_arm_units={:.2f} last_arms: {}\n"
			"[VR height] state={} progress={} armed={} arm_length_m={} fit_pending={}\n",
			height ? height->current.value : NAN, body_scale.load(), eye ? std::format("{:.3f}", *eye) : "unavailable",
			last_measured_cm.load(), measurements.load(), rejected.load(), waist_below_body.load(),
			arm_scale.load(), model_arm_units.load(), last_arms,
			measurement_state ? measurement_state->current.integer:0,measurement_progress ? measurement_progress->current.integer:0,armed,
			arm_length ? arm_length->current.value:0.f,fit_pending);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			const auto& setting = settings::player_height;
			height = dvars::register_float(setting.name, setting.default_value, setting.min, setting.max, game::DVAR_FLAG_SAVED,
				"Player height in cm; scales body equipment slots and physical crouch/prone thresholds");
			arm = dvars::register_float("vr_armScale", 1.f, min_arm_scale, max_arm_scale, game::DVAR_FLAG_SAVED,
				"Drawn arm length relative to the arm model; a T-pose height measurement sets it");
			arm_length=dvars::register_float("vr_measuredArmLength",0.f,0.f,1.2f,game::DVAR_FLAG_SAVED,
				"Measured shoulder-to-wrist length in metres; fitted when an arm model becomes available");
			fit_pending=arm_length->current.value>0;
			apply();
			scheduler::loop(apply, scheduler::pipeline::main, 250ms);
			scheduler::loop(sample_layout, scheduler::pipeline::server, 250ms);
			scheduler::loop(watch_hold, scheduler::pipeline::main);
			measured = dvars::register_int("vr_heightMeasured", 0, 0, 1000000, game::DVAR_FLAG_NONE,
				"Successful height measurements this session (VR options menu refresh)");
			measurement_state=dvars::register_int("vr_heightState",0,0,7,game::DVAR_FLAG_NONE,"Height measurement page state");
			measurement_progress=dvars::register_int("vr_heightProgress",0,0,1000,game::DVAR_FLAG_NONE,"Held T-pose measurement progress");
			// The menu uses begin without reopening itself; console/X open the page.
			command::add("vr_measure_height", [](const command::params& params) {
				const std::string_view action=params.size()>1 ? params.get(1):"arm";
				if(action=="cancel"){armed=false;capture.reset();publish(measurement::state::idle);}
				else if(action=="tick")page_seen=controller_input::clock::now();
				else if(action=="now")(void)measure();
				else
				{
					arm_measurement();
					if(action!="begin")command::execute("lui_open vr_height_measure",true);
				}
			});
			command::add("vr_height_status", [] {console::print_text(console::con_type_info, status());});
		}
	};
}

REGISTER_COMPONENT(vr::player_height::component)
