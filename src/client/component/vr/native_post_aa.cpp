#include <std_include.hpp>
#include "native_post_aa.hpp"
#include "native_post_aa_frame_thunk.hpp"
#include "native_post_aa_depth.hpp"
#include "native_post_aa_profile.hpp"
#include "native_aa_sample.hpp"
#include "native_render_contract.hpp"
#include "diagnostics/post_aa.hpp"
#include "component/d3d11.hpp"
#include "component/dxvk_runtime.hpp"
#include "component/console.hpp"
#include "component/command.hpp"
#include "component/scheduler.hpp"
#include "launcher/vr_settings.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <atomic>
#include <mutex>
#include <sstream>

namespace vr::native_post_aa
{
	namespace
	{
		constexpr std::uintptr_t dispatch_address = 0x140072BF0;
		constexpr std::uintptr_t frame_address = 0x14F79CBB4; // MWR AA history frame
		// MWR AA ring readers inside 0x140072C40 (H2 0x1402970DF / 0x14029737B):
		// temporal `mov ebx,[frame]` -> history target 16 + frame%4,
		// filmic `mov eax,[frame]` -> history target 20 + frame&1.
		constexpr std::uintptr_t temporal_frame_read = 0x140072E25, filmic_frame_read = 0x140073090;
		constexpr std::uint8_t temporal_frame_bytes[]{0x8B,0x1D,0x89,0x9D,0x72,0x0F};
		constexpr std::uint8_t filmic_frame_bytes[]{0x8B,0x05,0x1E,0x9B,0x72,0x0F};
		std::atomic_bool frame_reads_installed{};
		// vid_resize (0x1401B0430) only raises this flag; the main frame
		// (0x1401ACD50) then resets the renderer and recreates its targets.
		constexpr std::uintptr_t renderer_resize_request = 0x14D64D4FC;
		utils::hook::detour dispatch_hook;
		utils::hook::detour optimal_settings_hook;
		std::atomic_uint64_t optimal_preservations{};
		std::optional<unsigned> startup_menu_mode;
		std::atomic_bool resize_requested{};
		std::atomic_bool runtime_reset_pending{}, alive{true};
		std::atomic_uint64_t runtime_resets{};
		std::atomic_uint64_t guarded_dispatches{}, resize_requests{};
		struct frame_override { bool active{}; std::uint32_t phase{}; };
		thread_local frame_override current_frame{};
		std::uint32_t read_frame() noexcept
		{
			return current_frame.active ? current_frame.phase :
				*reinterpret_cast<const std::uint32_t*>(frame_address);
		}

		struct view_history
		{
			std::uint64_t pair{}, generation{}, revision{};
			std::uint32_t phase{};
			std::uint64_t rendered_at{};
			mode selected{};
			bool valid{};
		};
		struct state
		{
			std::array<view_history, 3> committed{}, pending{};
			std::uint64_t pending_pair{}, last_complete_pair{}, generation{}, revision{};
			std::uint32_t pending_mask{}, frame{};
			ID3D11DeviceContext* context{};
			depth_preserver depth;
		};
		state histories;
		std::mutex history_mutex;
		std::array<std::atomic_uint64_t, 3> completions{};
		std::array<std::atomic_uint32_t, 3> last_mode{};
		std::atomic_uint64_t failures{}, seeds{}, commits{};
		std::atomic_uint64_t reuses{};
		std::atomic_uint64_t jitter_phase_mismatches{};
		std::array<std::atomic_uint32_t,3> last_phase{};
		std::atomic<const char*> last_error{"none"};
		diagnostics::post_aa::history validation_failures;

		bool reject(const char* reason) noexcept
		{
			last_error.store(reason, std::memory_order_release);
			failures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		mode read_mode(const void* record) noexcept
		{
			mode result{};
			std::memcpy(&result, static_cast<const std::byte*>(record) + mode_offset, sizeof(result));
			return result;
		}
		template<std::size_t N> bool matches(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{};
			mask.fill(0xFF);
			return bool(utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<const void*>(address), {bytes, mask.data(), N}));
		}
		bool native_contract()
		{
			static const bool valid = []
			{
				// Exact MWR AA dispatcher and both history ring readers (rebound
				// from H2). No unverified renderer entry.
				constexpr std::uint8_t dispatch[]{0x48,0x83,0xEC,0x38,0x0F,0xB7,0x81,0x14,0x1E,0,0};
				return matches(dispatch_address, dispatch) && matches(temporal_frame_read, temporal_frame_bytes) &&
					matches(filmic_frame_read, filmic_frame_bytes);
			}();
			return valid;
		}
		void optimal_settings_stub(unsigned source, int flags)
		{
			// Switching either to DXVK or back to native D3D11 changes the adapter
			// identity. configure.csv can then turn an explicitly saved AA mode off.
			// Keep that one user choice; run the rest of native optimization normally.
			std::optional<std::string> saved;
			int selected{};
			if (startup_menu_mode) saved = menu_modes[*startup_menu_mode];
			const auto* option = game::Dvar_FindVar("r_postAA");
			if (option && option->type == game::dvar_type::enumeration)
			{
				selected = option->current.integer;
				if (selected >= 0 && selected <= 5 && selected < option->domain.enumeration.stringCount &&
					option->domain.enumeration.strings && option->domain.enumeration.strings[selected])
					saved = option->domain.enumeration.strings[selected];
			}
			optimal_settings_hook.invoke<void>(source, flags);
			if (saved)
			{
				game::Dvar_SetCommand(0x2CF72D9E, "r_postAA", saved->c_str());
				++optimal_preservations;
				console::info("[VR AA] Preserved %s after hardware optimization (renderer=%s)\n",
					saved->c_str(),dxvk_runtime::active() ? "DXVK":"D3D11");
			}
		}

		ID3D11RenderTargetView* target_view(std::uint32_t id) noexcept
		{
			ID3D11RenderTargetView* result{};
			std::memcpy(&result, reinterpret_cast<const void*>(native_render_contract::target_registry_base +
				id * native_render_contract::target_registry_stride + sizeof(void*)), sizeof(result));
			return result;
		}
		bool target_available(std::uint32_t target) noexcept
		{
			game::GfxImage* image{};
			std::memcpy(&image, reinterpret_cast<const void*>(native_render_contract::target_registry_base +
				target * native_render_contract::target_registry_stride), sizeof(image));
			return image && image->texture.map && image->texture.shaderView && target_view(target);
		}
		mode effective_mode(const void* record) noexcept
		{
			return transition_fallback(read_mode(record), target_available);
		}
		Microsoft::WRL::ComPtr<ID3D11Texture2D> target_texture(std::uint32_t id, game::GfxImage** resolved_image = nullptr)
		{
			Microsoft::WRL::ComPtr<ID3D11Resource> resource, sampled;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			game::GfxImage* image{};
			std::memcpy(&image, reinterpret_cast<const void*>(native_render_contract::target_registry_base +
				id * native_render_contract::target_registry_stride), sizeof(image));
			auto* view = target_view(id);
			if (view) view->GetResource(&resource);
			if (image && image->texture.shaderView) image->texture.shaderView->GetResource(&sampled);
			if (!resource || sampled != resource || FAILED(resource.As(&texture)) ||
				image->texture.map != texture.Get()) return {};
			if (resolved_image) *resolved_image = image;
			return texture;
		}
		bool matches_extent(std::uint32_t target, const D3D11_TEXTURE2D_DESC& expected,
			ID3D11Device* device)
		{
			game::GfxImage* image{};
			const auto texture = target_texture(target, &image);
			if (!texture) return false;
			D3D11_TEXTURE2D_DESC desc{};
			Microsoft::WRL::ComPtr<ID3D11Device> owner;
			texture->GetDesc(&desc);
			texture->GetDevice(&owner);
			D3D11_RENDER_TARGET_VIEW_DESC output{};
			D3D11_SHADER_RESOURCE_VIEW_DESC input{};
			target_view(target)->GetDesc(&output);
			image->texture.shaderView->GetDesc(&input);
			return owner.Get() == device && !target_rejection(target, desc, output, input, expected.Width, expected.Height);
		}

		// Native allocates SMAA/history targets only for the launch-time mode.
		// None -> SMAA must use FXAA until reset; SMAA -> T2x can use SMAA 1x.
		// The scene record is restored, so the requested mode survives the reset.
		bool dispatch_stub(void* record, const std::uint32_t input, const std::uint32_t scratch,
			const std::uint32_t output)
		{
			auto* const field = static_cast<std::byte*>(record) + mode_offset;
			const auto selected = read_mode(record);
			const auto fallback = effective_mode(record);
			if (fallback == selected)
			{
				if (selected >= mode::smaa && resize_requested.exchange(false, std::memory_order_acq_rel))
					runtime_reset_pending.store(true, std::memory_order_release);
				return dispatch_hook.invoke<bool>(record, input, scratch, output);
			}
			if (!resize_requested.exchange(true, std::memory_order_relaxed))
			{
				*reinterpret_cast<volatile std::int32_t*>(renderer_resize_request) = 1;
				resize_requests.fetch_add(1, std::memory_order_relaxed);
			}
			guarded_dispatches.fetch_add(1, std::memory_order_relaxed);
			std::memcpy(field, &fallback, sizeof(fallback));
			const auto restore = gsl::finally([&] { std::memcpy(field, &selected, sizeof(selected)); });
			return dispatch_hook.invoke<bool>(record, input, scratch, output);
		}

		diagnostics::post_aa::target observe_target(const std::uint32_t id)
		{
			game::GfxImage* image{};
			std::memcpy(&image, reinterpret_cast<const void*>(native_render_contract::target_registry_base +
				id * native_render_contract::target_registry_stride), sizeof(image));
			return diagnostics::post_aa::observe(id, image, image ? image->texture.map : nullptr,
				target_view(id), image ? image->texture.shaderView : nullptr);
		}

		bool reject_validation(const void* record, const native_display_contract::route route,
			const view_identity& view, mode selected, const char* reason,
			const std::uint32_t target = UINT32_MAX) noexcept
		{
			diagnostics::post_aa::failure sample;
			sample.tick = GetTickCount64(); sample.thread = GetCurrentThreadId();
			sample.record = reinterpret_cast<std::uintptr_t>(record);
			sample.selected = selected; sample.view = view; sample.route = route; sample.stage = reason;
			if (target != UINT32_MAX)
			{
				// The native GPU owner still holds the registry stable here. Never
				// defer these reads to the command worker after VR tears down.
				sample.targets_known = true;
				sample.reference = observe_target(route.destination);
				sample.failed = target == route.destination ? sample.reference : observe_target(target);
				const char* detail = diagnostics::post_aa::identity_rejection(sample.failed);
				if (!detail)
					detail = target == route.destination ?
						(native_display_contract::accepts(sample.failed.texture) ? nullptr : "display_descriptor") :
						diagnostics::post_aa::rejection(sample.failed, sample.reference);
				sample.detail = detail ? detail : "not_reproduced_at_capture";
				if (target == smaa_edge_target || target == smaa_blend_target)
				{
					sample.peer_known = true;
					sample.peer = observe_target(target == smaa_edge_target ? smaa_blend_target : smaa_edge_target);
				}
			}
			validation_failures.record(sample);
			return reject(reason);
		}
	}

	bool append_history_bindings(const void* record,
		std::array<engine_stereo_eye_resources::source_binding,
			engine_stereo_eye_resources::isolated_target_count>& bindings) noexcept
	{
		if (!record) return reject("missing_record");
		// Use the same temporary mode as the native dispatcher. Otherwise a
		// missing history rejects the stereo pair before the reset can run.
		const auto selected = effective_mode(record);
		if (!supported(selected)) return reject("unsupported_mode");
		for (std::size_t i = 0; i < history_targets.size(); ++i)
		{
			if (!needs_history(selected, history_targets[i])) continue;
			auto* view = target_view(history_targets[i]);
			if (!view) return reject("history_target_missing");
			bindings[i + 1] = {history_targets[i], engine_stereo_eye_resources::role::color, view};
		}
		return true;
	}

	bool select_display_target(const void* record, const native_display_contract::route route,
		std::uint32_t& destination, const view_identity& view) noexcept
	{
		if (!record || !route) return reject("display_identity");
		const auto selected = effective_mode(record);
		const auto fail = [&](const char* reason, std::uint32_t target = UINT32_MAX)
		{
			return reject_validation(record, route, view, selected, reason, target);
		};
		if (!supported(selected)) return fail("unsupported_mode");
		destination = route.destination;
		if (selected == mode::none) return true;
		if (!native_contract()) return fail("native_contract");
		if (temporal(selected) && !frame_reads_installed.load(std::memory_order_acquire)) return fail("frame_reader_hooks");
		const auto output = target_texture(route.destination);
		if (!output) return fail("display_target_missing", route.destination);
		D3D11_TEXTURE2D_DESC desc{};
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		output->GetDesc(&desc);
		output->GetDevice(&device);
		if (!native_display_contract::accepts(desc)) return fail("native_ldr_input", route.destination);
		if (!matches_extent(display_input_target, desc, device.Get())) return fail("native_ldr_input", display_input_target);
		if (selected >= mode::smaa && selected <= mode::filmic_smaa_t2x)
		{
			for (const auto target : {smaa_edge_target, smaa_blend_target})
				if (!matches_extent(target, desc, device.Get())) return fail("smaa_scratch_extent", target);
			if (filmic(selected) && !matches_extent(filmic_scratch_target, desc, device.Get()))
				return fail("filmic_scratch_extent", filmic_scratch_target);
			for (const auto target : history_targets)
				if (needs_history(selected, target) && !matches_extent(target, desc, device.Get()))
					return fail("history_extent", target);
		}
		destination = display_input_target;
		return true;
	}

	bool apply(void* record, const native_display_contract::route route,
		const view_identity& view) noexcept
	{
		if (!record || !route || !view.pair || !view.device_generation || view.eye >= 3)
			return reject("view_identity");
		try
		{
			const auto selected = effective_mode(record);
			if (!supported(selected)) return reject("unsupported_mode");
			const auto revision = engine_stereo_eye_resources::active_resource_revision(view.pair, view.eye);
			if (!revision) return reject("history_ownership");
			const auto graphics = d3d11::get_device_snapshot();
			if (!graphics || graphics.generation != view.device_generation) return reject("device_generation");
			std::lock_guard lock(history_mutex);
			if (histories.context != graphics.context.Get() || histories.generation != graphics.generation ||
				histories.revision != revision)
			{
				histories.committed = {};
				histories.revision = revision;
			}
			const auto frame = *reinterpret_cast<const std::uint32_t*>(frame_address);
			if (histories.pending_pair != view.pair)
			{
				if (histories.pending_pair) histories.committed = {};
				histories.pending = {};
				histories.pending_pair = view.pair;
				histories.pending_mask = 0;
				histories.frame = frame;
			}
			if (histories.frame != frame || (histories.pending_mask & (1u << view.eye)))
				return reject("native_frame_changed");
			histories.context = graphics.context.Get();
			histories.generation = graphics.generation;
			last_mode[view.eye].store(static_cast<std::uint32_t>(selected), std::memory_order_relaxed);
			if (selected == mode::none)
			{
				histories.pending_mask |= 1u << view.eye;
				return true;
			}
			if (!native_contract()) return reject("native_contract");
			const auto display = target_texture(display_input_target);
			if (!display) return reject("display_target_missing");
			const auto& previous = histories.committed[view.eye];
			const auto now = GetTickCount64();
			const bool reuse = previous.valid && !view.reset_history && previous.selected == selected &&
				previous.generation == view.device_generation && previous.revision == revision &&
				previous.pair == histories.last_complete_pair && view.pair == previous.pair + 1 && now - previous.rendered_at <= 250;
			// The frontend selected the projection jitter from this same pair.
			// Advancing just the ring while retaining a native-frame sample aliases
			// repeated/skipped native phases when game and HMD rates differ.
			const auto phase = sample_phase(view.pair);
			if(temporal(selected))
			{
				std::array<float,2> jitter{};
				std::memcpy(jitter.data(),static_cast<const std::byte*>(record)+0x160,sizeof(jitter));
				if(jitter!=sample_jitter(jitter,view.pair))++jitter_phase_mismatches;
			}
			last_phase[view.eye].store(phase,std::memory_order_relaxed);
			if(reuse)++reuses;
			view_history next{view.pair, view.device_generation, revision, phase, now, selected,true};
			// PostFX and the histories share native RGBA8 storage. Seed from this
			// eye's current image, retaining the original alpha as well as RGB.
			if ((temporal(selected) || filmic(selected)) && !reuse)
			{
				for (const auto id : history_targets)
				{
					if(!needs_history(selected,id))continue;
					auto* destination = engine_stereo_eye_resources::isolated_color_view(view.pair, view.eye, id);
					if (!destination) return reject("aa_history_seed");
					Microsoft::WRL::ComPtr<ID3D11Resource> history;
					destination->GetResource(&history);
					graphics.context->CopyResource(history.Get(), display.Get());
				}
				seeds.fetch_add(1, std::memory_order_relaxed);
			}
			const auto natural_frame = current_frame;
			const auto restore_frame = gsl::finally([&] { current_frame = natural_frame; });
			current_frame = {true, phase};
			// Match the scene-sized branch of 0x1407A7220. Native LDR source
			// and Filmic scratch stay intact; only the final output is redirected
			// to the VR display target. The HDR scene is never an AA destination.
			const auto draw = [&]
			{
				auto* source_state = *reinterpret_cast<void* const*>(0x140801FB0);
				utils::hook::invoke<void>(0x14059C0E0, source_state, static_cast<std::byte*>(record) + 0x170);
				return utils::hook::invoke<bool>(dispatch_address, record,
					display_input_target, filmic_scratch_target, route.destination);
			};
			ID3D11DepthStencilView* scene_depth{};
			std::memcpy(&scene_depth, reinterpret_cast<const void*>(native_render_contract::target_registry_base +
				4 * native_render_contract::target_registry_stride + 2 * sizeof(void*)), sizeof(scene_depth));
			const bool drawn = selected >= mode::smaa && selected <= mode::filmic_smaa_t2x ?
				histories.depth.preserve(graphics.context.Get(), scene_depth, draw) : draw();
			if (!drawn) return reject("native_dispatch");
			if (*reinterpret_cast<const std::uint32_t*>(frame_address) != frame)
				return reject("native_frame_changed");
			histories.pending[view.eye] = next;
			histories.pending_mask |= 1u << view.eye;
			completions[view.eye].fetch_add(1, std::memory_order_relaxed);
			last_error.store("none", std::memory_order_release);
			return true;
		}
		catch (...) { return reject("exception"); }
	}

	void finish_pair(const std::uint64_t pair, const bool successful) noexcept
	{
		std::lock_guard lock(history_mutex);
		if (histories.pending_pair != pair) return;
		if (successful && (histories.pending_mask & 3) == 3)
		{
			histories.committed = histories.pending;
			histories.last_complete_pair = pair;
			commits.fetch_add(1, std::memory_order_relaxed);
		}
		else histories.committed = {};
		histories.pending_pair = 0;
		histories.pending_mask = 0;
	}

	void invalidate_device(ID3D11DeviceContext* context, const std::uint64_t generation) noexcept
	{
		std::lock_guard lock(history_mutex);
		if (histories.context == context && histories.generation == generation) histories = {};
	}

	std::string status_text()
	{
		std::ostringstream out;
		out << "  native_post_aa: clock=projection_pair frame_hooks=" << frame_reads_installed.load()
			<< " mode(left/right/aux)=" << last_mode[0] << '/' << last_mode[1] << '/' << last_mode[2]
			<< " complete=" << completions[0] << '/' << completions[1] << '/' << completions[2]
			<< " history_seeds=" << seeds << " pair_commits=" << commits << " failures=" << failures
			<< " history_reuses=" << reuses << " sample_phase=" << last_phase[0] << '/' << last_phase[1] << '/' << last_phase[2]
			<< " jitter_phase_mismatches=" << jitter_phase_mismatches
			<< " last_error=" << last_error.load() << " runtime_switch(guarded/resize_requests)="
			<< guarded_dispatches << '/' << resize_requests << " vr_reinitializations=" << runtime_resets
			<< " optimal_preservations=" << optimal_preservations << '\n';
		out << validation_failures.format(GetTickCount64());
		return out.str();
	}

	class component final : public component_interface
	{
		void post_unpack() override
		{
			if (!native_contract()) { reject("native_contract"); return; }
			// Optimization runs before r_postAA's renderer enum is registered.
			// Capture its explicit profile choice before configure.csv can replace it.
			try { startup_menu_mode = saved_menu_mode(launcher_vr_settings::read_profile()); }
			catch (const std::exception& e) { console::warn("[VR AA] Could not read saved AA: %s\n", e.what()); }
			constexpr std::uint8_t optimal_bytes[]{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
			if (matches(0x1403A5E90, optimal_bytes))
				optimal_settings_hook.create(0x1403A5E90, optimal_settings_stub); // MWR optimal-settings application
			else console::error("[VR AA] Hardware optimization signature rejected; AA preservation unavailable\n");
			// vid_resize rebuilds native targets without DXGI ResizeBuffers, so the
			// runtime's resize listener never fires. Once the requested AA resources
			// exist, retire/reinitialize VR through its normal main-thread command.
			// Never enter runtime locks while this dispatcher owns the GPU context.
			scheduler::loop([]
			{
				if (!alive || !runtime_reset_pending.exchange(false, std::memory_order_acq_rel)) return;
				const auto* enabled = game::Dvar_FindVar("vr_enable");
				if (enabled && enabled->current.enabled)
				{
					command::execute("vr_reinit", true);
					++runtime_resets;
				}
			}, scheduler::pipeline::main);
			dispatch_hook.create(dispatch_address, dispatch_stub);
			// Install before native rendering starts. Each relay preserves the
			// original MOV's register contract and reads native time outside AA.
			auto* temporal = utils::hook::create_preserving_near_jump(temporal_frame_read,
				make_frame_read_thunk(reinterpret_cast<void*>(read_frame), frame_register::ebx));
			auto* filmic = utils::hook::create_preserving_near_jump(filmic_frame_read,
				make_frame_read_thunk(reinterpret_cast<void*>(read_frame), frame_register::eax));
			if (!temporal || !filmic) { reject("frame_reader_allocation"); return; }
			utils::hook::call(temporal_frame_read, temporal);
			utils::hook::nop(temporal_frame_read + 5, 1);
			utils::hook::call(filmic_frame_read, filmic);
			utils::hook::nop(filmic_frame_read + 5, 1);
			frame_reads_installed.store(true, std::memory_order_release);
		}
		void pre_destroy() override
		{
			alive.store(false, std::memory_order_release);
			(void)dispatch_hook.clear();
			(void)optimal_settings_hook.clear();
			if (!frame_reads_installed.exchange(false)) return;
			utils::hook::copy(temporal_frame_read, temporal_frame_bytes, sizeof(temporal_frame_bytes));
			utils::hook::copy(filmic_frame_read, filmic_frame_bytes, sizeof(filmic_frame_bytes));
		}
	};
}
REGISTER_COMPONENT(vr::native_post_aa::component)
