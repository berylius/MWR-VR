#pragma once
#include <filesystem>

namespace launcher_dxvk
{
	// Optional Vulkan renderer (vr_dxvk). The game loads the bundled DXVK from
	// overlord-mwr/dxvk for its own device only (component/dxvk_runtime.hpp).
	// sync removes DXVK copies older builds placed in the game folder (never
	// files that differ from the bundle, e.g. ReShade) and, when enabled,
	// checks that the bundle is present.
	void sync(const std::filesystem::path& game_directory, bool enabled);
}
