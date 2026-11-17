#include <std_include.hpp>
#include "scene_models.hpp"
#include "scene_surface_layout.hpp"
#include "scene_surface_storage.hpp"
#include "scene_model_culling.hpp"
#include "scene_model_culling_bridge.hpp"
#include "scene_dobj_visibility.hpp"
#include "scene_rigid_visibility.hpp"
#include "scene_model_identity.hpp"
#include "scene_model_identity_bridge.hpp"
#include "scheduler_context.hpp"
#include "fastfiles.hpp"
#include "game/xmodel_index_contract.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include "command.hpp"
#include "console.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/native_memory.hpp>
#include <utils/startup_callbacks.hpp>

namespace scene_models
{
	namespace
	{
		utils::hook::detour generate_hook;
		utils::hook::detour prepare_hook;
		utils::hook::detour model_index_hook;
		// Cover all retained subset producers together; admission must not depend
		// on which weapon family happened to prepare its independent parts first.
		// Counted magazines retain four states per receiver alongside chamber,
		// action and equipment models. Keep this shared budget bounded and large
		// enough for the comprehensive weapon scene without evicting queued models.
		identity::registry<512> model_identities;
		std::atomic_size_t registration_failures{},asset_retirements{};
		identity::pool model_pool() noexcept
		{
			using namespace game::xmodel_index_contract;
			const auto base=reinterpret_cast<std::uintptr_t>(game::g_assetPool[game::ASSET_TYPE_XMODEL]);
			const auto count=game::g_poolSize[game::ASSET_TYPE_XMODEL];
			if (!base || base>UINTPTR_MAX-pool_header || count<=0) return {};
			return {base+pool_header,static_cast<std::size_t>(count),pool_stride};
		}
		std::uintptr_t canonical_model(std::uintptr_t model,std::uintptr_t native_first) noexcept
		{
			const auto pool=model_pool();
			if (pool.first==native_first)
				if (const auto source=model_identities.source(pool,model)) return *source;
			// Never clamp arbitrary engine pointers to slot zero. Unregistered stock
			// calls preserve native behavior; our submit boundary rejects such inputs.
			return model;
		}
		utils::startup_callbacks<placement_callback> placement_consumers;
		utils::startup_callbacks<visibility_callback> visibility_consumers;
		utils::startup_callbacks<rigid_visibility_callback> rigid_visibility_consumers;
		std::atomic_uint64_t visibility_selected{},visibility_admitted{},visibility_recovered{},visibility_failed{};
		std::atomic_uint64_t rigid_selected{},rigid_admitted{},rigid_recovered{},rigid_failed{},rigid_repacked{};
		std::atomic_uint64_t runtime_builds{},runtime_build_failures{};
		std::atomic_uint32_t runtime_arena_peak{},runtime_failure_cursor{};
		bool restore_rigid_part(std::byte* record);
		std::atomic<build_observer> build_observations{};
		std::atomic<scene_observer> scene_observations{};
		std::atomic_uint64_t scene_observation_sequence{};
		thread_local preparation preparing{};
		struct culling_scope { game::XModel* model{}; float padding{}; bool preserve_primary{}; };
		thread_local culling_scope culling{};
		std::atomic_uint64_t rigid_visibility_selected{},rigid_visibility_recovered{};
		unsigned filter_primary_visibility(std::uint8_t* banks,unsigned rejected) noexcept
		{
			if(!culling.preserve_primary)return rejected;
			++rigid_visibility_selected;
			const auto result=preserve_primary_visibility({banks,4},rejected,true);
			if(result!=rejected)++rigid_visibility_recovered;
			return result;
		}
		float filter_radius(game::XModel* model)
		{
			const auto radius = reinterpret_cast<float(*)(game::XModel*)>(0x1404800E0)(model);
			const auto padded=radius + (model == culling.model ? culling.padding : 0.f);
			return std::isfinite(padded) ? padded : radius;
		}
		void prepare(void* command)
		{
			const auto before = preparing;
			const auto leave = gsl::finally([&] { preparing = before; });
			preparing = {};
			if (command)
			{
				std::memcpy(&preparing.record,command,sizeof(void*));
			}
			prepare_hook.invoke<void>(command);
			if(command && !visibility_consumers.empty())
			{
				unsigned begin{},end{};
				std::memcpy(&begin,static_cast<const std::byte*>(command)+0xc,4);
				std::memcpy(&end,static_cast<const std::byte*>(command)+0x10,4);
				const auto result=dobj_visibility::preserve(
					{reinterpret_cast<std::byte*>(0x151C55978),dobj_visibility::capacity*dobj_visibility::stride},
					{reinterpret_cast<std::uint8_t*>(0x151C6B978),dobj_visibility::capacity},begin,end,
					[](const void* object){for(const auto select:visibility_consumers)if(select(object))return true;return false;},
					[](void* record){utils::hook::invoke<void>(0x140571320,record);});
				visibility_selected+=result.selected;visibility_admitted+=result.admitted;
				visibility_recovered+=result.recovered;visibility_failed+=result.failed;
			}
			if(command && !rigid_visibility_consumers.empty())
			{
				unsigned begin{},end{};
				std::memcpy(&begin,static_cast<const std::byte*>(command)+0x14,4);
				std::memcpy(&end,static_cast<const std::byte*>(command)+0x18,4);
				const auto result=rigid_visibility::preserve(
					{reinterpret_cast<std::byte*>(0x151C6C990),rigid_visibility::capacity*rigid_visibility::stride},
					{reinterpret_cast<std::uint8_t*>(0x151C9F990),rigid_visibility::views*rigid_visibility::capacity},begin,end,
					[](const void* entry){for(const auto select:rigid_visibility_consumers)if(select(entry))return true;return false;},
					restore_rigid_part);
				rigid_selected+=result.selected;rigid_admitted+=result.admitted;rigid_recovered+=result.recovered;
				rigid_failed+=result.failed;rigid_repacked+=result.repacked;
			}
		}
		// MWR 0x1405713A0 takes 10 arguments; H2's trailing colour pointer is absent.
		using build_fn = int(*)(void*, game::XModel*, void*, const game::GfxPlacement*, float,
			const game::GfxPlacement*, float, bool, int, int);
		int build(void* output, game::XModel* model, void* object, const game::GfxPlacement* current,
			float scale, const game::GfxPlacement* previous, float previous_scale, bool motion,
			int lod, int material_lod)
		{
			game::GfxPlacement placed{}, history{};
			const auto observe=build_observations.load(std::memory_order_acquire);
			build_observation observation{preparing.record,output,model,nullptr,current,current,build_outcome::retained,0};
			const auto finish=[&](int result) {
				if (observe && observation.owner)
				{
					observation.resolved=current;observation.native_result=result;observe(observation);
				}
				return result;
			};
			if (!placement_consumers.empty() && preparing.record && current && previous)
			{
				// Native skeletal preparation may have taken time since job entry.
				// Sample once at the first rigid consumer, then reuse for every part.
				if(preparing.at==std::chrono::steady_clock::time_point{})preparing.at=std::chrono::steady_clock::now();
				placed = *current; history = *previous;
				bool replaced=false;
				for (const auto consumer:placement_consumers)
				{
					const auto result=consumer(preparing,output,placed,history);
					if (result!=placement_result::unchanged) observation.owner=consumer;
					if (result==placement_result::omit)
					{observation.outcome=build_outcome::omitted;return finish(0);}
					if (result==placement_result::replace)
					{
						if (replaced)
						{observation.outcome=build_outcome::conflicting;return finish(0);}
						replaced=true; current=&placed; previous=&history;
						observation.outcome=build_outcome::replaced;
					}
				}
			}
			// Native copies quaternion and fixed-point position into the surface
			// payload, including optional motion history; it retains neither pointer.
			scene_surface_storage::reserve_mock_test_prefix();
			const int native_result=reinterpret_cast<build_fn>(0x1405713A0)(output,model,object,current,scale,
				previous,previous_scale,motion,lod,material_lod);
			const auto pool=model_pool();
			if(!pool.index(reinterpret_cast<std::uintptr_t>(model)) && model_identities.source(pool,reinterpret_cast<std::uintptr_t>(model)))
			{
				++runtime_builds;
				// H1 R_BuildXModelSurfaces 0x1405717F8/801; H2 observation offsets differ.
				std::uintptr_t frontend{};std::uint32_t cursor{};
				if(utils::native_memory::read_bytes(&frontend,reinterpret_cast<const void*>(0x15182A680),sizeof(frontend)) && frontend)
					utils::native_memory::read_bytes(&cursor,reinterpret_cast<const void*>(frontend+0x540c14),sizeof(cursor));
				auto peak=runtime_arena_peak.load();while(peak<cursor && !runtime_arena_peak.compare_exchange_weak(peak,cursor)){}
				if(!native_result)
				{
					runtime_failure_cursor=cursor;
					const auto count=++runtime_build_failures;
					if(count<=16 || count%1000==0)console::info("[VR runtime model packing] failure=%llu cursor=%u native_limit=%zu lod=%d model=%s\n",count,cursor,scene_surface_storage::capacity,lod,model->name);
				}
			}
			return finish(native_result);
		}
		utils::startup_callbacks<callback> callbacks;
		bool restore_rigid_part(std::byte* record)
		{
			// MWR R_AddSceneEntSurfaces 0x14053AD29..5B supplies these exact
			// arguments. The native builder validates LOD/streaming and capacity.
			game::XModel* model{};void* object{};game::GfxPlacement current{},previous{};
			float scale{},previous_scale{};unsigned flags{};
			std::memcpy(&model,record+8,sizeof(model));std::memcpy(&object,record+0x10,sizeof(object));
			const auto pool=model_pool();
			if(!model || pool.index(reinterpret_cast<std::uintptr_t>(model)) ||
				!model_identities.source(pool,reinterpret_cast<std::uintptr_t>(model)))return false;
			std::memcpy(&current,record+0x18,sizeof(current));std::memcpy(&previous,record+0x38,sizeof(previous));
			std::memcpy(&scale,record+0x34,4);std::memcpy(&previous_scale,record+0x54,4);std::memcpy(&flags,record+0x5c,4);
			const auto lod=std::to_integer<unsigned char>(record[0x58]);
			const auto material_lod=static_cast<signed char>(std::to_integer<unsigned char>(record[0x7d]));
			return build(record,model,object,&current,scale,&previous,previous_scale,(flags&0x800)!=0,lod,material_lod)!=0;
		}
		std::atomic_bool installed{};
		void generate(std::uint32_t client, std::uint32_t index, void* scratch, void* selected, void* slot, void* output)
		{
			const auto observe=scene_observations.load(std::memory_order_acquire);
			scene_observation observation{0,0,index,scratch,slot};
			if (observe) {observation.sequence=++scene_observation_sequence;observe(observation);}
			for (const auto callback:callbacks) callback();
			if (observe) {observation.stage=1;observe(observation);}
			generate_hook.invoke<void>(client, index, scratch, selected, slot, output);
			if (observe) {observation.stage=2;observe(observation);}
		}
	}
	void on_submit(callback function)
	{
		callbacks.add(function);
	}
	void observe_builds(build_observer observer) noexcept
	{
		build_observations.store(observer,std::memory_order_release);
	}
	void observe_scenes(scene_observer observer) noexcept
	{
		scene_observations.store(observer,std::memory_order_release);
	}
	bool ready() noexcept { return installed.load(); }
	bool register_runtime_models(std::span<const runtime_model> models) noexcept
	{
		// A precomputed immutable animation is registered atomically, so a
		// rejected clip cannot leave aliases pointing to half-retired frames.
		if (!ready() || !scheduler::is_executing(scheduler::pipeline::main) || models.empty() || models.size()>128) return false;
		std::array<identity::alias,128> batch{};
		for (std::size_t n=0;n<models.size();++n)
			batch[n]={reinterpret_cast<std::uintptr_t>(models[n].descriptor),reinterpret_cast<std::uintptr_t>(models[n].source)};
		const bool accepted=model_identities.publish(model_pool(),{batch.data(),models.size()});
		if (!accepted) ++registration_failures;
		return accepted;
	}
	namespace
	{
		std::atomic<std::uint64_t> submit_calls{}, submit_admitted{};
		std::atomic<const char*> last_submit_model{""};
		std::array<std::atomic<float>,3> last_origin{};
		std::array<std::atomic<float>,5> last_quat_scale{};
		std::atomic<const game::XModel*> last_model{};
		thread_local bool probing{};
	}
	std::string runtime_model_status()
	{
		return std::format("runtime_models={}/512 registration_failures={} asset_retirements={}\n",
			model_identities.size(),registration_failures.load(),asset_retirements.load()) + submit_status();
	}
	std::string submit_status()
	{
		const auto* ps = game::CL_IsCgameInitialized() ? game::CG_GetPredictedPlayerState(0) : nullptr;
		// The last model (and its name) may belong to a zone unloaded since; copy
		// both through guarded reads instead of dereferencing them.
		std::array<char, 64> name{};
		const auto* name_source = last_submit_model.load();
		if (!name_source || !utils::native_memory::read_bytes(name.data(), name_source, name.size() - 1)) name[0] = 0;
		return std::format("scene_models submits={} admitted={} last={} origin=({:.1f},{:.1f},{:.1f}) player=({:.1f},{:.1f},{:.1f}){}\n", submit_calls.load(), submit_admitted.load(), name.data(),
			last_origin[0].load(), last_origin[1].load(), last_origin[2].load(), ps ? ps->origin[0] : 0.f, ps ? ps->origin[1] : 0.f, ps ? ps->origin[2] : 0.f, [] {
			const auto* source = last_model.load();
			game::XModel copy{};
			if (!source || !utils::native_memory::read_bytes(&copy, source, sizeof(copy))) return std::string{};
			const auto* m = &copy;
			return std::format(" quat=({:.3f},{:.3f},{:.3f},{:.3f}) scale={:.3f}", last_quat_scale[0].load(), last_quat_scale[1].load(), last_quat_scale[2].load(), last_quat_scale[3].load(), last_quat_scale[4].load()) + std::format(" model={} lods={} lod0(dist={:.0f} surfs={} ptr={} modelSurfs={}) lod1(dist={:.0f} surfs={})",
				static_cast<const void*>(source), int(m->numLods), m->lodInfo[0].dist, m->lodInfo[0].numsurfs,
				static_cast<const void*>(m->lodInfo[0].surfs), static_cast<const void*>(m->lodInfo[0].modelSurfs),
				m->lodInfo[1].dist, m->lodInfo[1].numsurfs);
			}()) + std::format("rigid_held_visibility selected={} recovered={}\n",
				rigid_visibility_selected.load(),rigid_visibility_recovered.load());
	}
	void submit(game::XModel* model, game::GfxScaledPlacement* placement, unsigned flags,
		unsigned short* lighting_handle, float* lit, float* unlit, float* emissive, float padding,visibility policy)
	{
		if (!ready() || !model || !placement ||
			!model_identities.source(model_pool(),reinterpret_cast<std::uintptr_t>(model))) return;
		const auto before = culling;
		const auto leave = gsl::finally([&] { culling = before; });
		// Native scales the returned radius. Convert the bounded WORLD-unit
		// allowance to local units; an invalid scale keeps native behavior.
		culling = {model, local_radius_padding(padding,placement->scale),policy==visibility::held_item};
		// MWR port diagnostic: the scene XModel slot counter advances only when
		// R_FilterXModelIntoScene admits the model (frustum + LOD).
		const auto* slots = reinterpret_cast<const volatile std::uint32_t*>(0x151C6C984);
		const auto before_slots = *slots;
		game::R_FilterXModelIntoScene(model,placement,flags,lighting_handle,lit,unlit,emissive);
		if (probing) return;
		++submit_calls;
		if (*slots != before_slots) ++submit_admitted;
		last_submit_model = model->name;
		last_model = model;
		for (int i = 0; i < 3; ++i) last_origin[i] = placement->base.origin[i];
		for (int i = 0; i < 4; ++i) last_quat_scale[i] = placement->base.quat[i];
		last_quat_scale[4] = placement->scale;
	}
	void on_prepare_placement(placement_callback function)
	{
		placement_consumers.add(function);
	}
	void on_preserve_hand_visibility(visibility_callback function)
	{
		visibility_consumers.add(function);
	}
	void on_preserve_rigid_visibility(rigid_visibility_callback function)
	{
		rigid_visibility_consumers.add(function);
	}
	std::string hand_visibility_status()
	{
		return std::format("runtime_model_packing builds={} failed={} arena_peak={} last_failure_cursor={} native_limit={}\n",
			runtime_builds.load(),runtime_build_failures.load(),runtime_arena_peak.load(),runtime_failure_cursor.load(),scene_surface_storage::capacity)+
			std::format("held_weapon_visibility selected={} admitted={} recovered={} failed={}\n",
			visibility_selected.load(),visibility_admitted.load(),visibility_recovered.load(),visibility_failed.load())+
			std::format("held_rigid_visibility selected={} admitted={} recovered={} failed={} repacked={}\n",
				rigid_selected.load(),rigid_admitted.load(),rigid_recovered.load(),rigid_failed.load(),rigid_repacked.load());
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			// MWR port diagnostic: draw a named XModel 30 units ahead of the eye
			// through this scene-model path ("vr_scene_probe" alone clears it).
			command::add("vr_scene_probe", [](const command::params& params)
			{
				static game::XModel* probe{};
				static float distance{30};
				static bool registered{};
				distance = params.size() > 2 ? float(std::atof(params.get(2))) : 30.f;
				probe = params.size() > 1 ? game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL, params.get(1), 0).model : nullptr;
				if (!registered)
				{
					registered = true;
					on_submit([]
					{
						if (!probe || !game::CL_IsCgameInitialized()) return;
						const auto* ps = game::CG_GetPredictedPlayerState(0);
						if (!ps) return;
						static unsigned short lightings[4]{};
						probing = true;
						const auto restore = gsl::finally([] { probing = false; });
						if (distance == 0)
						{
							// Mirror the last presenter placement exactly.
							game::GfxScaledPlacement placement{};
							for (int i = 0; i < 4; ++i) placement.base.quat[i] = last_quat_scale[i];
							for (int i = 0; i < 3; ++i) placement.base.origin[i] = last_origin[i];
							placement.scale = 1;
							float color[4]{1, 1, 1, 1};
							submit(probe, &placement, 0, &lightings[0], color, color, color, 4);
							return;
						}
						const float offsets[4][2]{{distance, 0}, {-distance, 0}, {0, distance}, {0, -distance}};
						for (int i = 0; i < 4; ++i)
						{
							game::GfxScaledPlacement placement{};
							placement.base.quat[3] = 1;
							placement.base.origin[0] = ps->origin[0] + offsets[i][0];
							placement.base.origin[1] = ps->origin[1] + offsets[i][1];
							placement.base.origin[2] = ps->origin[2] + 55;
							placement.scale = 1;
							float color[4]{1, 1, 1, 1};
							submit(probe, &placement, 0, &lightings[i], color, color, color, 4);
						}
					});
				}
			});
			fastfiles::on_pre_unload([] {
				// Same drained native boundary used by DObj and surface-subset
				// owners. Pool addresses alone do not describe an asset lifetime.
				model_identities.reset_after_drain();++asset_retirements;
			});
			using namespace game::xmodel_index_contract;
			static_assert(sizeof(game::XModel)==pool_stride);
			// Hook AFTER the pool-base LEA: fastfiles owns its relocatable operand.
			// This ordering works whether that component has relocated the pool yet
			// or not. All native index callers retain the original leaf result/ABI.
			constexpr std::uint8_t lea[]{0x48,0x8d,0x05}, lea_mask[]{0xff,0xff,0xff};
			std::array<std::uint8_t,identity::getter_tail.size()> tail_mask{}; tail_mask.fill(0xff);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(getter),{lea,lea_mask,3}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(mapping_boundary),
					{identity::getter_tail.data(),tail_mask.data(),tail_mask.size()}))
				throw std::runtime_error("native XModel resource identity contract rejected");
			auto* bridge=utils::hook::assemble([](utils::hook::assembler& a) {
				identity::emit_bridge(a,reinterpret_cast<std::uintptr_t>(canonical_model));
			});
			if (!bridge) throw std::runtime_error("native XModel resource identity bridge allocation failed");
			model_index_hook.create(mapping_boundary,bridge);
			// Existing GUI model-preview boundary, centralized so gameplay and
			// tooling do not stack detours or depend on each other's state.
			// MWR R_GenerateSortedDrawSurfs 0x14058C670 (H2 0x140778E60), +5 after its first push.
			constexpr std::uint8_t bytes[]{0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe9};
			std::array<std::uint8_t, sizeof(bytes)> mask{}; mask.fill(0xff);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14058C675), {bytes,mask.data(),mask.size()}))
				throw std::runtime_error("native scene model submission contract rejected");
			constexpr std::uint8_t prepare_bytes[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
			std::array<std::uint8_t,sizeof(prepare_bytes)> prepare_mask{}; prepare_mask.fill(0xff);
			constexpr std::uint8_t build_call[]{0xe8,0x40,0x66,0x03,0x00};
			constexpr std::uint8_t call_mask[]{0xff,0xff,0xff,0xff,0xff};
			// Live R_FilterXModelIntoScene scales this radius and adds it to the
			// plane distance in its frustum admission loop. Do not patch the shared
			// getter or mutate XModel data retained by other queued submissions.
			constexpr std::uint8_t radius_call[]{0xe8,0xa6,0x45,0xf4,0xff};
			constexpr std::uint8_t primary_boundary[]{0xf3,0x0f,0x10,0x0d,0xa5,0x16,0x26,0x0f};
			std::array<std::uint8_t,sizeof(primary_boundary)> primary_mask{};primary_mask.fill(0xff);
			constexpr std::uint8_t skin_bytes[]{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x41,0x48};
			std::array<std::uint8_t,sizeof(skin_bytes)> skin_mask{};skin_mask.fill(0xff);
			constexpr std::uint8_t range_bytes[]{0x8b,0x71,0x0c};
			constexpr std::uint8_t range_mask[]{0xff,0xff,0xff};
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14053A4E0),
				{prepare_bytes,prepare_mask.data(),prepare_mask.size()}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140571320),{skin_bytes,skin_mask.data(),skin_mask.size()}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14053A51C),{range_bytes,range_mask,3}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14053AD5B),
				{build_call,call_mask,5}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14053BB35),{radius_call,call_mask,5}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14053BBFB),{primary_boundary,primary_mask.data(),primary_mask.size()}) ||
				!utils::hook_validation::validate_executable_target(reinterpret_cast<void*>(0x1404800E0)) ||
				!utils::hook_validation::validate_executable_target(reinterpret_cast<void*>(0x1405713A0)))
				throw std::runtime_error("native rigid-surface preparation contract rejected");
			generate_hook.create(0x14058C670, generate);
			prepare_hook.create(0x14053A4E0,prepare);
			utils::hook::call(0x14053AD5B,build);
			utils::hook::call(0x14053BB35,filter_radius);
			auto* primary_bridge=utils::hook::assemble([](utils::hook::assembler& a) {
				emit_primary_visibility_bridge(a,reinterpret_cast<std::uintptr_t>(filter_primary_visibility),0x14F79D2A8);
			});
			auto* primary_relay=primary_bridge ? utils::hook::create_preserving_near_jump(0x14053BBFB,primary_bridge):nullptr;
			if(!primary_relay)throw std::runtime_error("native held-item visibility bridge allocation failed");
			utils::hook::call(0x14053BBFB,primary_relay);utils::hook::nop(0x14053BC00,3);
			installed = true;
		}
		void pre_destroy() override { installed = false; }
	};
}
REGISTER_COMPONENT(scene_models::component)
