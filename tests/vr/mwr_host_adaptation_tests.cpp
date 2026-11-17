#include <std_include.hpp>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <typeinfo>
#include "component/game_data.hpp"
#include "game/mwr/profile.hpp"
#include "loader/component_loader.hpp"
#include "component/vr/projection_bounds.hpp"
#include "component/vr/ui_texture_limits.hpp"
#include "component/vr/engine_stereo_view.hpp"
#include "component/vr/openxr_d3d11.hpp"
#include "component/vr/gameplay/grip_edges.hpp"
using vr::hand;
#include "weapon_release_retry_tests.hpp"
#include "weapon_carry_motion_tests.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
namespace { unsigned constructors{}; void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); } }
namespace unported_host { class component : public component_interface { public: component() = delete; }; }
REGISTER_COMPONENT(unported_host::component)
namespace scheduler { class component : public component_interface { public: component() { ++constructors; } }; }
REGISTER_COMPONENT(scheduler::component)
void component_loader::register_component(std::unique_ptr<component_interface>&&) {}

namespace
{
	void write(const fs::path& p, const std::string& value) { std::ofstream(p, std::ios::binary) << value; }
	std::string read(const fs::path& p) { std::ifstream s(p, std::ios::binary); return {std::istreambuf_iterator<char>(s), {}}; }
	void profiles(const fs::path& output)
	{
		const auto before = fs::current_path();
		const auto area = output / ("mwr-profile-fixture-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
		fs::create_directories(area / "players2");
		write(area / "players2/config.cfg", "original config");
		write(area / "players2/savegame.svg", "original checkpoint");
		write(area / "players2/settings_s.zip.h1", "settings");
		write(area / "players2/h1_example.stat", "progress");
		write(area / "players2/steam_autocloud.vdf", "not imported");
		fs::current_path(area);
		try
		{
			game_data::initialize_players_folder();
			const auto target = fs::path(game_data::get_config_file_path()).parent_path();
			require(read(target / "savegame.svg") == "original checkpoint", "first seed imports save");
			require(read(target / "h1_example.stat") == "progress", "first seed imports progression");
			require(!fs::exists(target / "steam_autocloud.vdf"), "Steam cloud ownership is not imported");
			write(target / "savegame.svg", "VR progress");
			game::mwr::profile::seed("players2", target, "again");
			require(read(target / "savegame.svg") == "VR progress", "private progress wins");
			const fs::path unicode_target = L"private-\uD55C\uAE00/players2";
			game::mwr::profile::seed("players2", unicode_target, "unicode");
			require(read(unicode_target / "savegame.svg") == "original checkpoint", "Unicode profile directory imports");
			auto create = reinterpret_cast<decltype(&CreateFileW)>(game_data::resolve_profile_import("KERNEL32.dll", "CreateFileW"));
			require(create != nullptr, "native import routes profile opens");
			const auto file = create(L"players2/native.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			require(file != INVALID_HANDLE_VALUE, "native private file opens");
			CloseHandle(file);
			require(fs::exists(target / "native.txt") && !fs::exists("players2/native.txt"), "native writes use private directory");
			auto attributes = reinterpret_cast<decltype(&GetFileAttributesA)>(game_data::resolve_profile_import("kernel32.dll", "GetFileAttributesA"));
			require(attributes("players2/native.txt") != INVALID_FILE_ATTRIBUTES, "native attributes use private directory");
			auto move = reinterpret_cast<decltype(&MoveFileExW)>(game_data::resolve_profile_import("kernel32.dll", "MoveFileExW"));
			require(move(L"players2/native.txt", L"players2/renamed.txt", 0), "native rename uses private directory");
			auto erase = reinterpret_cast<decltype(&DeleteFileA)>(game_data::resolve_profile_import("kernel32.dll", "DeleteFileA"));
			require(erase("players2/renamed.txt"), "native delete uses private directory");
			WIN32_FIND_DATAW found{};
			auto find = reinterpret_cast<decltype(&FindFirstFileExW)>(game_data::resolve_profile_import("kernel32.dll", "FindFirstFileExW"));
			const auto search = find(L"players2/savegame.*", FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, 0);
			require(search != INVALID_HANDLE_VALUE, "native enumeration uses private directory"); FindClose(search);
			require(game_data::resolve_profile_import("other.dll", "CreateFileW") == nullptr, "unrelated imports untouched");
			require(game::mwr::profile::redirect("players2", target, "players20/config.cfg") == fs::path("players20/config.cfg"), "prefix boundary is exact");
			require(game::mwr::profile::redirect("players2", target, "players2/../main/file") == fs::path("players2/../main/file"), "normalized non-profile paths stay native");
			require(game::mwr::profile::redirect("players2", target, "PLAYERS2/config.cfg") == fs::absolute(target / "config.cfg"), "Windows case variants redirected");
			fs::create_directories("invalid/config.cfg");
			bool failed{};
			try { game::mwr::profile::seed("invalid", "failed-profile", "fail"); } catch (...) { failed = true; }
			require(failed && !fs::exists("failed-profile") && !fs::exists("failed-profile.import-fail"), "failed import leaves no partial profile");
			require(read("players2/config.cfg") == "original config" && read("players2/savegame.svg") == "original checkpoint", "original files remain unchanged");
		}
		catch (...) { fs::current_path(before); throw; }
		fs::current_path(before);
	}
	void projections()
	{
		using namespace vr;
		for (const auto& p : {engine_stereo_bridge::eye_projection{-.9f,1.3f,-1.2f,.8f},
			engine_stereo_bridge::eye_projection{.1f,2,-1,1},
			engine_stereo_bridge::eye_projection{-2,-.1f,.1f,2},
			engine_stereo_bridge::eye_projection{-.01f,.01f,-.01f,.01f},
			engine_stereo_bridge::eye_projection{-25,25,-25,25}})
		{
			std::array<std::uint8_t, engine_stereo_view::h2_view_slot_size> natural{};
			const float one = 1;
			for (auto offset : {0x10C,0x11C,0x12C,0x148}) std::memcpy(natural.data()+offset, &one, 4);
			std::array<engine_stereo_bridge::render_config,2> configs{};
			for (unsigned eye=0;eye<2;++eye)
			{
				auto& c=configs[eye]; c.frame_id=c.pair_id=c.publication=1;
				c.output_eye=c.view_eye=eye; c.half_eye_offset_units=1.25f; c.eyes={p,p};
			}
			engine_stereo_view::slot_pair slots;
			require(engine_stereo_view::derive(natural.data(),configs,slots), "valid off-axis/narrow/wide eye derived");
			for (const auto& eye : slots.eyes)
			{
				engine_stereo_bridge::eye_projection decoded{};
				require(engine_stereo_view::read_projection(eye,decoded), "native projection decodes");
				require(std::abs(decoded.tan_left-p.tan_left)<.0001f && std::abs(decoded.tan_up-p.tan_up)<.0001f, "projection retains optical bounds");
			}
			engine_stereo_view::culling_union_slot culling;
			require(engine_stereo_view::derive_culling_union(natural.data(),configs,culling), "native culling admits same optical family");
			require(culling.tan_left<=p.tan_left && culling.tan_right>=p.tan_right, "culling conservatively encloses off-axis eyes");
		}
		require(!projection_bounds::valid(1,1,-1,1), "zero width rejected");
		require(!projection_bounds::valid(2,1,-1,1), "reversed width rejected");
		require(!projection_bounds::valid(-1,INFINITY,-1,1), "nonfinite rejected");
	}
	void toggles()
	{
		using namespace vr::gameplay::weapons;
		using namespace carry;
		for (unsigned h=0;h<2;++h)
		{
			const auto bit=1u<<h;
			grip_edge_gate gate; vr::controller_input::digital_sampler sampler;
			vr::controller_input::frame f; f.focused=true; f.reference_generation=1; f.sampled_at+=1s;
			std::array<hold,2> owners{};
			const auto tick=[&](bool down,unsigned mask,bool pose=true,int ms=10)
			{
				++f.sequence; f.sampled_at+=std::chrono::milliseconds(ms); f.grip[h].valid=pose;
				f.squeeze[h]=sampler.sample(true,down,f.sampled_at);
				return gate.consume(f,true,f.sampled_at,owners,mask);
			};
			tick(false,0); require(tick(true,0).pressed==bit,"initial press acquires");
			owners[h].weapon=42;owners[h].instance_generation=7;owners[h].revision=1;owners[h].rear=vr::hand(h);
			require(!tick(false,bit).released,"first physical release keeps toggled gun");
			auto e=tick(true,bit);
			require(e.released==bit && e.consumed==bit && !e.pressed,"second press drops without simultaneous pickup");
			require(gate.retry_release(owners[h],e.released)==bit,"blocked toggle release queues retry while button down");
			require(!tick(false,bit).released,"retry throttled after button release");
			e=tick(false,bit,true,100);
			require(e.released==bit && e.retried==bit && e.consumed==bit,"toggle obstruction recovery retains intent");
			require(gate.retry_release(owners[h],e.released)==bit,"second rejection retains retry");
			++owners[h].instance_generation;
			require(!tick(false,bit,true,100).released,"replacement instance never inherits release");
			e=tick(true,bit,false);
			require(e.deferred==bit && e.consumed==bit && !e.released,"toggle waits for this hand pose");
			require(!tick(false,0).released,"changing to hold cancels deferred toggle");
			require(!tick(true,0).released && tick(false,0).released==bit,"hold mode keeps physical release semantics");
		}
	}
	void menu_capacity()
	{
		require(vr::ui_texture_limits::valid_extent(4096u,2160u), "DCI 4K menu admitted");
		require(vr::ui_texture_limits::valid_extent(10000u,1000u), "wide valid menu admitted");
		require(!vr::ui_texture_limits::valid_extent(16385u,16u) && !vr::ui_texture_limits::valid_extent(0u,16u), "invalid device dimensions rejected");
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)), "WARP test device");
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width=4096;desc.Height=2160;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
		desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture)), "4K UI allocation succeeds");
	}
#if H2V_OPENXR_HEADERS_AVAILABLE
	unsigned wait_calls{}, release_calls{}, destroy_calls{};
	XrResult next_wait=XR_TIMEOUT_EXPIRED;
	XrResult XRAPI_PTR wait_image(XrSwapchain,const XrSwapchainImageWaitInfo*) { ++wait_calls; return next_wait; }
	XrResult XRAPI_PTR release_image(XrSwapchain,const XrSwapchainImageReleaseInfo*) { ++release_calls; return XR_SUCCESS; }
	XrResult XRAPI_PTR destroy_image(XrSwapchain) { ++destroy_calls; return XR_SUCCESS; }
	void leases()
	{
		vr::openxr::dispatch_table xr{};
		xr.wait_swapchain_image=wait_image;xr.release_swapchain_image=release_image;xr.destroy_swapchain=destroy_image;
		vr::openxr::eye_swapchain eye;
		eye.handle=reinterpret_cast<XrSwapchain>(std::uintptr_t(1));eye.acquired=true;eye.acquired_index=2;
		XrResult result{};
		require(!vr::openxr::wait_acquired_image(xr,eye,result) && result==XR_TIMEOUT_EXPIRED && eye.acquired && !eye.waited && eye.acquired_index==2,"timeout retains exact acquisition");
		require(!vr::openxr::release_acquired_image(xr,eye,result) && !release_calls,"unwaited image never released");
		require(!vr::openxr::destroy_eye_swapchain(xr,eye,result) && !destroy_calls,"timeout during resize/teardown retains handle");
		next_wait=XR_SUCCESS;
		require(vr::openxr::wait_acquired_image(xr,eye,result),"retry completes same wait");
		require(vr::openxr::wait_acquired_image(xr,eye,result) && wait_calls==3,"completed wait not repeated");
		require(vr::openxr::destroy_eye_swapchain(xr,eye,result) && release_calls==1 && destroy_calls==1,"wait release destroy order");
	}
#endif
}
int main(int argc, char** argv)
{
	try
	{
		require(constructors==1,"only admitted component constructed");
		profiles(argc > 1 ? fs::absolute(argv[1]) : fs::temp_directory_path()); std::cout<<"MWR_PRIVATE_PROFILE_OK\n";
		projections(); std::cout<<"MWR_PROJECTION_COMPATIBILITY_OK\n";
		toggles(); std::cout<<"MWR_GRIP_TOGGLE_OK\n";
		menu_capacity(); std::cout<<"MWR_4K_MENU_CAPACITY_OK\n";
#if H2V_OPENXR_HEADERS_AVAILABLE
		leases(); std::cout<<"MWR_XR_WAIT_RETRY_OK\n";
#endif
		unsigned failures{};
		auto check=[&](bool ok,const char* text){if(!ok){++failures;std::cerr<<text<<'\n';}};
		weapon_release_retry_tests::run(check); weapon_carry_motion_tests::run(check);
		require(failures==0,"existing hold/release/motion regression");
		std::cout<<"MWR_HOST_ADAPTATIONS_OK\n";
		return 0;
	}
	catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
