#include "launcher/dxvk_install.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
static void write(const fs::path& p, const char* text) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << text; }
static bool throws(const fs::path& g, bool on) { try { launcher_dxvk::sync(g, on); return false; } catch (const std::exception& e) { std::cout << "  error: " << e.what() << "\n"; return true; } }
int main()
{
	const auto g = fs::temp_directory_path() / "mwrvr_dxvk_sync";
	fs::remove_all(g);
	write(g / "overlord-mwr/dxvk/d3d11.dll", "DXVK-D3D11");
	write(g / "overlord-mwr/dxvk/dxgi.dll", "DXVK-DXGI");
	launcher_dxvk::sync(g, true);                     // on: bundle only, game folder untouched
	assert(!fs::exists(g / "d3d11.dll") && !fs::exists(g / "dxgi.dll"));
	write(g / "d3d11.dll", "DXVK-D3D11");             // copies from an older build
	write(g / "dxgi.dll", "DXVK-DXGI");
	launcher_dxvk::sync(g, true);                     // removed even when on
	assert(!fs::exists(g / "d3d11.dll") && !fs::exists(g / "dxgi.dll"));
	write(g / "d3d11.dll", "DXVK-D3D11");
	launcher_dxvk::sync(g, false);                    // and when off
	assert(!fs::exists(g / "d3d11.dll"));
	write(g / "dxgi.dll", "RESHADE");                 // foreign wrapper
	launcher_dxvk::sync(g, true);
	launcher_dxvk::sync(g, false);                    // never delete foreign
	{ std::ifstream f(g / "dxgi.dll"); std::string s; f >> s; assert(s == "RESHADE"); }
	fs::remove(g / "dxgi.dll");
	fs::remove_all(g / "overlord-mwr");
	assert(throws(g, true));                          // bundle missing
	assert(!throws(g, false));
	fs::remove_all(g);
	std::cout << "DXVK_SYNC_OK\n";
}
