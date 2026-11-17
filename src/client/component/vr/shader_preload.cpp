#include <std_include.hpp>
#include "shader_preload.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <array>
#include <atomic>
#include <sstream>

namespace vr::shader_preload
{
	namespace
	{
		constexpr std::array names{
			"r_preloadShaders", "r_preloadShadersFrontendAllow", "r_preloadShadersAfterCinematic"};
		constexpr std::array<std::uint32_t, 3> hashes{0x2950CB6D, 0x3502C932, 0x8BF035B2};
		utils::hook::detour set_variant_hook;
		std::atomic_bool installed{};
		std::atomic_uint64_t blocked{};

		void set_variant_stub(game::dvar_t* option, game::dvar_value* value, game::DvarSetSource source)
		{
			if (option && option->type == game::dvar_type::boolean && value)
				for (const auto hash : hashes)
					if (static_cast<std::uint32_t>(option->hash) == hash)
					{
						if (value->enabled) ++blocked;
						game::dvar_value off{};
						set_variant_hook.invoke<void>(option, &off, source);
						return;
					}
			set_variant_hook.invoke<void>(option, value, source);
		}
	}

	void append_status(std::ostream& out)
	{
		out << "  shader_preload: policy=disabled hook=" << installed.load()
			<< " blocked_enables=" << blocked.load() << '\n';
	}

	class component final : public component_interface
	{
		void post_unpack() override
		{
			// MWR Dvar_SetVariant: typed setters (bool 0x14041B820), console,
			// config/LUI strings (0x14041BE20), and configure.csv all converge here.
			// H2's two-argument setter hooks are intentionally not ported in dvars.cpp.
			constexpr std::uintptr_t setter = 0x14041C190;
			constexpr std::array<std::uint8_t, 16> expected{
				0x48,0x85,0xC9,0x0F,0x84,0xCF,0x02,0,0,0x53,0x56,0x57,0x48,0x83,0xEC,0x50};
			std::array<std::uint8_t, expected.size()> mask{};
			mask.fill(0xFF);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<const void*>(setter),
				{expected.data(), mask.data(), expected.size()}))
			{
				console::error("[VR shaders] Native setter signature rejected; preload lock unavailable\n");
				return;
			}
			set_variant_hook.create(setter, set_variant_stub);
			constexpr auto flags = game::DVAR_FLAG_SAVED | game::DVAR_FLAG_READ;
			for (std::size_t i{}; i < names.size(); ++i)
			{
				// Register before the profile/optimal settings can create a temporary
				// string dvar. Native registration later retains a false reset value.
				dvars::override::register_bool(names[i], false, flags);
				game::Dvar_RegisterBool(static_cast<int>(hashes[i]), names[i], false, flags);
			}
			installed = true;
			console::info("[VR shaders] Shader preloading is locked off (frontend, level, after cinematic)\n");
			command::add("vr_shader_preload_status", []
			{
				std::ostringstream out;
				append_status(out);
				console::print_text(console::con_type_info, out.str());
			});
		}
	};
}

REGISTER_COMPONENT(vr::shader_preload::component)
