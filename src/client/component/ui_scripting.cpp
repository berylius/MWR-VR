#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "scheduler.hpp"
#include "command.hpp"

#include "filesystem.hpp"
#include "scripting.hpp"
#include "fastfiles.hpp"
#include "console.hpp"
#include "camera.hpp"
#include "vr/gameplay/weapon_hud_source.hpp"
#include "vr/gameplay/weapon_carry_runtime.hpp"
#include "vr/native_menu.hpp"
#include "vr/hud_prompts.hpp"

#include "game/ui_scripting/execution.hpp"
#include "game/scripting/execution.hpp"

#include "ui_scripting.hpp"
#include "ui_script_modules.hpp"
#include "product.hpp"
#include "campaign_unlock_request.hpp"
#include "vr/settings.hpp"
#include "vr/settings_menu.hpp"
#include <utils/flags.hpp>

#include <utils/string.hpp>
#include <utils/hook.hpp>
#include <utils/io.hpp>

namespace ui_scripting
{
	namespace
	{
		const auto lui_common = utils::nt::load_resource(LUI_COMMON);
		const auto lui_updater = utils::nt::load_resource(LUI_UPDATER);
		const auto lui_campaign_unlock = utils::nt::load_resource(LUI_CAMPAIGN_UNLOCK);
		const auto lui_subtitles = utils::nt::load_resource(LUI_SUBTITLES);
		const auto lui_vr_options = utils::nt::load_resource(LUI_VR_OPTIONS);
		const auto lui_vr_height_art = utils::nt::load_resource(LUI_VR_HEIGHT_ART);
		const auto lua_json = utils::nt::load_resource(LUA_JSON);

		using lua_function_t = std::function<arguments(const function_arguments& args)>;
		std::vector<std::unique_ptr<lua_function_t>> lua_functions;

		utils::hook::detour hks_start_hook;
		utils::hook::detour hks_shutdown_hook;
		utils::hook::detour hks_package_require_hook;

		struct globals_t
		{
			std::string in_require_script;
			std::unordered_map<std::string, std::string> loaded_scripts;
			bool load_raw_script{};
			std::string raw_script_name{};
		};

		globals_t globals{};

		bool is_loaded_script(const std::string& name)
		{
			return globals.loaded_scripts.contains(name);
		}

		std::string get_root_script(const std::string& name)
		{
			const auto itr = globals.loaded_scripts.find(name);
			return itr == globals.loaded_scripts.end() ? std::string() : itr->second;
		}

		table get_globals()
		{
			const auto state = *game::hks::lua_state;
			return state->globals.v.table;
		}

		void print_error(const std::string& error)
		{
			console::error("************** LUI script execution error **************\n");
			console::error("%s\n", error.data());
			console::error("********************************************************\n");
		}

		void print_loading_script(const std::string& name)
		{
			console::info("Loading LUI script '%s'\n", name.data());
		}

		std::string get_current_script()
		{
			const auto state = *game::hks::lua_state;
			game::hks::lua_Debug info{};
			game::hks::hksi_lua_getstack(state, 1, &info);
			game::hks::hksi_lua_getinfo(state, "nSl", &info);
			return info.short_src;
		}

		int load_buffer(const std::string& name, const std::string& data)
		{
			const auto state = *game::hks::lua_state;
			const auto sharing_mode = state->m_global->m_bytecodeSharingMode;
			state->m_global->m_bytecodeSharingMode = game::hks::HKS_BYTECODE_SHARING_ON;
			const auto _0 = gsl::finally([&]()
			{
				state->m_global->m_bytecodeSharingMode = sharing_mode;
			});

			game::hks::HksCompilerSettings compiler_settings{};
			return game::hks::hksi_hksL_loadbuffer(state, &compiler_settings, data.data(), data.size(), name.data());
		}

		void load_script(const std::string& name, const std::string& data)
		{
			globals.loaded_scripts[name] = name;

			const auto lua = get_globals();
			const auto load_results = lua["loadstring"](data, name);

			if (load_results[0].is<function>())
			{
				const auto results = lua["pcall"](load_results);
				if (!results[0].as<bool>())
				{
					print_error(results[1].as<std::string>());
				}
			}
			else if (load_results[1].is<std::string>())
			{
				print_error(load_results[1].as<std::string>());
			}
		}

		void load_scripts()
		{
			// Use the same overlay precedence as ordinary filesystem reads. A
			// complete local package may coexist with the old AppData cache.
			for (const auto& script : modules::discover(filesystem::get_search_paths_rev()))
			{
				std::string data{};
				if (!utils::io::read_file(script.initializer, &data))
				{
					console::error("Failed to read LUI script '%s'\n", script.initializer.data());
					continue; // Do not execute a stale cached copy after selecting an override.
				}
				print_loading_script(script.initializer);
				load_script(script.initializer, data);
			}
		}

		script_value json_to_lua(const nlohmann::json& json)
		{
			if (json.is_object())
			{
				table object;
				for (const auto& [key, value] : json.items())
				{
					object[key] = json_to_lua(value);
				}
				return object;
			}

			if (json.is_array())
			{
				table array;
				auto index = 1;
				for (const auto& value : json.array())
				{
					array[index++] = json_to_lua(value);
				}
				return array;
			}

			if (json.is_boolean())
			{
				return json.get<bool>();
			}

			if (json.is_number_integer())
			{
				return json.get<int>();
			}

			if (json.is_number_float())
			{
				return json.get<float>();
			}

			if (json.is_string())
			{
				return json.get<std::string>();
			}

			return {};
		}

		nlohmann::json lua_to_json(const script_value& value)
		{
			if (value.is<bool>())
			{
				return value.as<bool>();
			}
			
			if (value.is<int>())
			{
				return value.as<int>();
			}

			if (value.is<float>())
			{
				return value.as<float>();
			}

			if (value.is<std::string>())
			{
				return value.as<std::string>();
			}
			
			if (value.get_raw().t == game::hks::TNIL)
			{
				return {};
			}

			throw std::runtime_error("lua value must be of primitive type (boolean, integer, float, string)");
		}

		void setup_functions()
		{
			const auto lua = get_globals();
			const auto native_localize=lua["Engine"]["Localize"].as<function>();
			lua["Engine"]["Localize"]=[native_localize](const variadic_args& args)->arguments
			{
				if(args.size()==1&&args[0].is<std::string>())
				{
					const auto key = args[0].as<std::string>();
					if (const auto fixed = product::fixed_menu_text(key)) return {std::string(fixed)};
					if (const auto alias = product::menu_text_alias(key)) return native_localize(std::string(alias));
					if (product::product_description(key))
					{
						const auto original = native_localize.call(args);
						if (original.size() == 1 && original[0].is<std::string>())
							return {product::menu_description(key, original[0].as<std::string>())};
						return original;
					}
					const auto text=vr::hud_prompts::replace(vr::hud_prompts::source::lui,key);
					if(text)return {*text};
				}
				return native_localize.call(args);
			};
			lua["vr_weapon_hud"] = table();
			lua["vr_weapon_hud"]["active"] = [] {return vr::gameplay::weapons::carry::active();};
			lua["vr_weapon_hud"]["count"] = [] {return int(vr::gameplay::weapon_hud::source_count);};
			lua["vr_weapon_hud"]["source"] = [](int index)
			{
				const auto v=vr::gameplay::weapon_hud::source(static_cast<unsigned>(index));
				table result;
				result["valid"]=v.owner.weapon!=0;result["weapon"]=int(v.owner.weapon);
				result["loaded"]=v.loaded;result["reserve"]=v.reserve;result["capacity"]=v.capacity;
				result["maximumReserve"]=v.maximum_reserve;result["clipType"]=v.clip_type;
				result["lowThreshold"]=v.low_threshold;result["name"]=v.name;
				result["definition"]=int(v.definition);result["feed"]=int(v.channel);
				return result;
			};
			lua["vr_weapon_hud"]["commit"] = [](int index,bool visible) {vr::gameplay::weapon_hud::source_rendered(static_cast<unsigned>(index),visible);};

			lua["io"] = table();
			lua["io"]["fileexists"] = filesystem::safe_io_func<bool>(utils::io::file_exists);
			lua["io"]["writefile"] = filesystem::safe_write_file;
			lua["io"]["filesize"] = filesystem::safe_io_func<size_t>(utils::io::file_size);
			lua["io"]["createdirectory"] = filesystem::safe_io_func<bool>(utils::io::create_directory);
			lua["io"]["directoryexists"] = filesystem::safe_io_func<bool>(utils::io::directory_exists);
			lua["io"]["directoryisempty"] = filesystem::safe_io_func<bool>(utils::io::directory_is_empty);
			lua["io"]["listfiles"] = filesystem::safe_io_func<std::vector<std::string>>(utils::io::list_files);
			lua["io"]["removefile"] = filesystem::safe_io_func<bool>(utils::io::remove_file);
			lua["io"]["removedirectory"] = filesystem::safe_io_func<bool>(utils::io::remove_directory);
			lua["io"]["readfile"] = filesystem::safe_io_func<std::string>(
				static_cast<std::string(*)(const std::string&)>(utils::io::read_file));


			using game = table;
			auto game_type = game();
			lua["game"] = game_type;

			// Options > VR (vr_options.lua): launcher-schema settings this build registers.
			game_type["vrsettings"] = [](const game&, const script_value& language)
			{
				table result;
				int index = 1;
				for (const auto& e : vr::settings_menu::catalog(language.as<int>()))
				{
					table row;
					row["name"] = e.name;row["category"] = e.category;row["categoryLabel"] = e.category_label;
					row["group"] = e.group;row["groupLabel"] = e.group_label;
					row["label"] = e.label;row["description"] = e.description;
					row["type"] = std::string(e.type == vr::settings_menu::kind::toggle ? "toggle" :
						e.type == vr::settings_menu::kind::choice ? "choice" : "number");
					row["min"] = e.min;row["max"] = e.max;row["step"] = e.step;row["scale"] = e.scale;
					row["enabledLevel"] = e.enabled_level;
					row["whenName"] = e.when_name;row["whenValue"] = e.when_value;
					table conditions;
					int condition_index=1;
					for (const auto& condition : e.conditions)
					{
						table item;
						item["name"]=condition.value;item["value"]=condition.label;
						conditions.set(condition_index++,item);
					}
					row["conditions"]=conditions;
					table options;
					int option_index = 1;
					for (const auto& option : e.options)
					{
						table item;
						item["value"] = option.value;item["label"] = option.label;
						options.set(option_index++, item);
					}
					row["options"] = options;
					result.set(index++, row);
				}
				return result;
			};
			game_type["vrsettingtext"] = [](const game&, const script_value& language, const script_value& key)
			{
				return vr::settings_menu::text(language.as<int>(), key.as<std::string>());
			};
			game_type["vrsettingget"] = [](const game&, const script_value& name)
			{
				return vr::settings_menu::value(name.as<std::string>());
			};
			game_type["vrsettingset"] = [](const game&, const script_value& name, const script_value& value)
			{
				return vr::settings_menu::set(name.as<std::string>(), value.as<std::string>());
			};

			game_type["applycampaignunlock"] = [](const game&, const script_value& apply)
			{
				static campaign_unlock::request request([]
				{
					const auto value = utils::flags::get_flag("campaign-unlock").value_or("0");
					try { campaign_unlock::parse(value); return value; }
					catch (const std::exception& error)
					{
						console::error("[Campaign unlock] %s\n", error.what());
						return std::string("0");
					}
				}());
				if (!request.take(::game::Com_InFrontend())) return;
				try
				{
					apply.as<function>()();
					console::info("[Campaign unlock] Applied; native profile save requested.\n");
				}
				catch (const std::exception& error)
				{
					// A failed request must neither break the menu nor replay on VM reload.
					console::error("[Campaign unlock] Failed: %s\n", error.what());
				}
			};

			game_type["applysubtitles"] = [](const game&, const script_value& apply)
			{
				// Process-owned, so a VM reload or returning from Audio cannot
				// overwrite a subtitle choice made later in the current session.
				static bool applied{};
				if (applied || !::game::Com_InFrontend()) return;
				const auto* option = ::game::Dvar_FindVar(vr::settings::subtitles.name);
				if (!option || option->type != ::game::dvar_type::boolean) return;
				applied = true;
				try
				{
					apply.as<function>()(option->current.enabled);
					console::info("[Subtitles] Applied launcher preference: %s.\n", option->current.enabled ? "enabled" : "disabled");
				}
				catch (const std::exception& error)
				{
					console::error("[Subtitles] Failed to apply launcher preference: %s\n", error.what());
				}
			};

			game_type["sharedset"] = [](const game&, const std::string& key, const std::string& value)
			{
				scripting::shared_table.access([key, value](scripting::shared_table_t& table)
				{
					table[key] = value;
				});
			};

			game_type["sharedget"] = [](const game&, const std::string& key)
			{
				std::string result;
				scripting::shared_table.access([key, &result](scripting::shared_table_t& table)
				{
					result = table[key];
				});
				return result;
			};

			game_type["sharedclear"] = [](const game&)
			{
				scripting::shared_table.access([](scripting::shared_table_t& table)
				{
					table.clear();
				});
			};

			game_type["assetlist"] = [](const game&, const std::string& type_string)
			{
				auto table_ = table();
				auto index = 1;
				auto type_index = -1;

				for (auto i = 0; i < ::game::XAssetType::ASSET_TYPE_COUNT; i++)
				{
					if (type_string == ::game::g_assetNames[i])
					{
						type_index = i;
					}
				}

				if (type_index == -1)
				{
					throw std::runtime_error("Asset type does not exist");
				}

				const auto type = static_cast<::game::XAssetType>(type_index);
				fastfiles::enum_assets(type, [type, &table_, &index](const ::game::XAssetHeader header)
				{
					const auto asset = ::game::XAsset{type, header};
					const std::string asset_name = ::game::DB_GetXAssetName(&asset);
					table_[index++] = asset_name;
				}, true);

				return table_;
			};

			game_type["getweapondisplayname"] = [](const game&, const std::string& name)
			{
				const auto alternate = name.starts_with("alt_");
				const auto weapon = ::game::G_GetWeaponForName(name.data());

				char buffer[0x400] = {0};
				::game::CG_GetWeaponDisplayName(weapon, alternate, buffer, 0x400);

				return std::string(buffer);
			};


			lua["string"]["escapelocalization"] = [](const std::string& str)
			{
				return "\x1F"s.append(str);
			};

			lua["string"]["el"] = lua["string"]["escapelocalization"];


			using player = table;
			auto player_type = player();
			lua["player"] = player_type;

			player_type["notify"] = [](const player&, const std::string& name, const variadic_args& va)
			{
				if (!::game::CL_IsCgameInitialized() || !::game::g_entities[0].client)
				{
					throw std::runtime_error("Not in game");
				}

				const auto to_string = get_globals()["tostring"];
				const auto arguments = get_return_values();
				std::vector<std::string> args{};
				for (const auto& value : va)
				{
					const auto value_str = to_string(value);

					args.push_back(value_str[0].as<std::string>());
				}

				::scheduler::once([name, args]()
				{
					try
					{
						std::vector<scripting::script_value> arguments{};

						for (const auto& arg : args)
						{
							arguments.push_back(arg);
						}

						const auto player = scripting::call("getentbynum", {0}).as<scripting::entity>();
						scripting::notify(player, name, arguments);
					}
					catch (...)
					{
					}
				}, ::scheduler::pipeline::server);
			};

			// MWR exposes only admitted native/VR bindings. H2 updater, mod-loader,
			// language-restart, achievements and stats services are not installed.

			table camera_table;
			lua["camera"] = camera_table;

			camera::clear_lua();
			camera_table["enablefreemove"] = camera::enable_free_move;
			camera_table["disablefreemove"] = camera::disable_free_move;
			camera_table["isfreemoveenabled"] = camera::is_free_move_enabled;
			camera_table["setposition"] = camera::set_camera_position;
			camera_table["setangles"] = camera::set_camera_angles;
			camera_table["getposition"] = camera::get_camera_position;
			camera_table["setusingoriginoverride"] = camera::set_using_origin_override;
			camera_table["setusinganglesoverride"] = camera::set_using_angles_override;
			camera_table["setcallback"] = camera::set_callback;
		}

		void start()
		{
			globals = {};

			const auto lua = get_globals();
			lua["EnableGlobals"]();

			table keydown_event{};
			keydown_event["key"] = "";
			keydown_event["keynum"] = 0;

			table keyup_event{};
			keyup_event["key"] = "";
			keyup_event["keynum"] = 0;

			lua["LUI"]["CachedEvents"]["keydown"] = keydown_event;
			lua["LUI"]["CachedEvents"]["keyup"] = keyup_event;

			setup_functions();

			lua["print"] = [](const variadic_args& va)
			{
				std::string buffer{};
				const auto to_string = get_globals()["tostring"];

				for (auto i = 0; i < va.size(); i++)
				{
					const auto& arg = va[i];
					const auto str = to_string(arg)[0].as<std::string>();
					buffer.append(str);

					if (i < va.size() - 1)
					{
						buffer.append("\t");
					}
				}

				console::info("%s\n", buffer.data());
			};

			lua["table"]["unpack"] = lua["unpack"];
			lua["luiglobals"] = lua;

			// MWR: printmemoryusage (H2 0x14031F470) not rebound. Only the shared
			// helpers and the VR options menu load; the H2 updater, campaign unlock,
			// subtitle profile and on-disk mod scripts stay off (not ported).
			load_script("lui_common", lui_common);
			load_script("lua_json", lua_json);
			load_script("lui_vr_height_art", lui_vr_height_art);
			load_script("lui_vr_options", lui_vr_options);
		}

		void try_start()
		{
			try
			{
				start();
			}
			catch (const std::exception& e)
			{
				console::error("Failed to load LUI scripts: %s\n", e.what());
			}
		}

		void hks_start_stub(char a1)
		{
			const auto _0 = gsl::finally(&try_start);
			return hks_start_hook.invoke<void>(a1);
		}

		void hks_shutdown_stub()
		{
			vr::native_menu::reset_vm();
			camera::clear_lua();
			lua_functions.clear();
			globals = {};
			hks_shutdown_hook.invoke<void>();
		}

		void* hks_package_require_stub(game::hks::lua_State* state)
		{
			const auto script = get_current_script();
			const auto root = get_root_script(script);
			globals.in_require_script = root;
			return hks_package_require_hook.invoke<void*>(state);
		}

		game::XAssetHeader db_find_xasset_header_stub(game::XAssetType type, const char* name, int allow_create_default)
		{
			if (!is_loaded_script(globals.in_require_script))
			{
				return game::DB_FindXAssetHeader(type, name, allow_create_default);
			}

			const auto folder = globals.in_require_script.substr(0, globals.in_require_script.find_last_of("/\\"));
			std::string name_ = utils::string::replace(name, "~", ".");
			const std::string target_script = folder + "/" + name_ + ".lua";

			if (utils::io::file_exists(target_script))
			{
				globals.load_raw_script = true;
				globals.raw_script_name = target_script;
				return static_cast<game::XAssetHeader>(reinterpret_cast<game::LuaFile*>(1));
			}
			else if (name_.starts_with("ui/LUI/"))
			{
				return game::DB_FindXAssetHeader(type, name, allow_create_default);
			}

			return static_cast<game::XAssetHeader>(nullptr);
		}

		int hks_load_stub(game::hks::lua_State* state, void* compiler_options, 
			void* reader, void* reader_data, const char* chunk_name)
		{
			if (globals.load_raw_script)
			{
				globals.load_raw_script = false;
				globals.loaded_scripts[globals.raw_script_name] = globals.in_require_script;
				return load_buffer(globals.raw_script_name, utils::io::read_file(globals.raw_script_name));
			}

			return utils::hook::invoke<int>(0x1400B46F0 /* H2 0x1402D9410 */, state, compiler_options, reader, reader_data, chunk_name);
		}
		
		int main_handler(game::hks::lua_State* state)
		{
			static std::string error_str;
			auto error = false;

			try
			{
				const auto value = state->m_apistack.base[-1];
				if (value.t != game::hks::TCFUNCTION)
				{
					return 0;
				}

				const auto closure = value.v.cClosure;
				if (closure->m_numUpvalues < 1)
				{
					return 0;
				}

				const auto function = reinterpret_cast<lua_function_t*>(closure->m_upvalues->v.i64);
				const auto args = get_return_values();
				const auto results = function->operator()(args);

				for (const auto& result : results)
				{
					push_value(result);
				}

				return static_cast<int>(results.size());
			}
			catch (const std::exception& e)
			{
				error_str = e.what();
				error = true;
			}

			if (error)
			{
				game::hks::hksi_luaL_error(state, error_str.data());
			}

			return 0;
		}

		int removed_function_stub(game::hks::lua_State* /*state*/)
		{
			return 0;
		}
	}

	template <typename F>
	game::hks::cclosure* convert_function(F f)
	{
		const auto state = *game::hks::lua_state;
		const auto top = state->m_apistack.top;

		game::hks::HksObject func_ptr{};
		func_ptr.t = game::hks::TUI64;
		func_ptr.v.i64 = 0;

		push_value(func_ptr);
		const auto closure = game::hks::cclosure_Create(state, main_handler, 1, 0, 0);
		state->m_apistack.top = top;

		const auto function = wrap_function(f);
		const auto& iterator = lua_functions.insert(lua_functions.end(), std::make_unique<lua_function_t>(function));
		const auto ptr = iterator->get();

		closure->m_upvalues[0].t = game::hks::TUI64;
		closure->m_upvalues[0].v.i64 = reinterpret_cast<size_t>(ptr);

		return closure;
	}

	class component final : public component_interface
	{
	public:

		void post_unpack() override
		{
			// MWR SP rebinding (h1-mod SP offsets; call sites verified in h1_sp64_clean:
			// DB_FindXAssetHeader 0x1401F1120 type 0x3d, then the hks loader 0x1400B46F0).
			utils::hook::call(0x1400E72CB /* H2 0x14030BF2B */, db_find_xasset_header_stub);
			utils::hook::call(0x1400E7419 /* H2 0x14030C079 */, db_find_xasset_header_stub);
			utils::hook::call(0x1400E74A4 /* H2 0x14030C104 */, hks_load_stub);

			hks_package_require_hook.create(0x140090070 /* H2 0x1402B4DA0 */, hks_package_require_stub);
			hks_start_hook.create(0x140103C50 /* H2 0x140328BE0 */, hks_start_stub);
			hks_shutdown_hook.create(0x1400FB370 /* H2 0x1403203B0 */, hks_shutdown_stub);

			// MWR: the H2 unsafe-library removal below has no SP rebinding (h1-mod
			// strips them in MP only). No on-disk Lua loads, so the stock LUI
			// environment is unchanged.
#if 0 // H2 addresses, kept for the future SP rebinding.
			// remove unsafe functions
			utils::hook::nop(0x1402D885A, 1);
			utils::hook::jump(0x14031E700, 0x1402D86E0);

			utils::hook::jump(0x1402BFCC0, removed_function_stub); // io
			utils::hook::jump(0x1402C0150, removed_function_stub); // os
			utils::hook::jump(0x1402C1020, removed_function_stub); // serialize
			utils::hook::jump(0x1402C0FF0, removed_function_stub); // hks
			utils::hook::jump(0x1402C0470, removed_function_stub); // debug
			utils::hook::nop(0x1402BFC48, 5); // coroutine

			// profile
			utils::hook::jump(0x1402B6250, removed_function_stub);
			utils::hook::jump(0x1402B6260, removed_function_stub);
			utils::hook::jump(0x1402B6270, removed_function_stub);
			utils::hook::jump(0x1402B6330, removed_function_stub);

			utils::hook::jump(0x1402B7FD0, removed_function_stub);
			utils::hook::jump(0x1402B7C40, removed_function_stub);
			utils::hook::jump(0x1402BAC30, removed_function_stub);

			utils::hook::jump(0x1402B5480, removed_function_stub);
			utils::hook::jump(0x1402C2030, removed_function_stub);
			utils::hook::jump(0x1402C2180, removed_function_stub);

			utils::hook::jump(0x1402B6F70, removed_function_stub);
			utils::hook::jump(0x1402BD890, removed_function_stub);
#endif
		}
	};
}

REGISTER_COMPONENT(ui_scripting::component)
