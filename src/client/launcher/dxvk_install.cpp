#include <std_include.hpp>
#include "dxvk_install.hpp"
#include <fstream>

namespace launcher_dxvk
{
	namespace
	{
		constexpr const wchar_t* libraries[]{L"d3d11.dll", L"dxgi.dll"};

		bool same_content(const std::filesystem::path& a, const std::filesystem::path& b)
		{
			std::error_code error;
			const auto size = std::filesystem::file_size(a, error);
			if (error || size != std::filesystem::file_size(b, error) || error) return false;
			std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
			if (!left || !right) return false;
			std::vector<char> x(1 << 16), y(1 << 16);
			while (left && right)
			{
				left.read(x.data(), static_cast<std::streamsize>(x.size()));
				right.read(y.data(), static_cast<std::streamsize>(y.size()));
				if (left.gcount() != right.gcount() ||
					std::memcmp(x.data(), y.data(), static_cast<std::size_t>(left.gcount())) != 0) return false;
			}
			return left.eof() && right.eof();
		}

		std::string narrow(const std::filesystem::path& path)
		{
			const auto text = path.u8string();
			return {text.begin(), text.end()};
		}

		constexpr int retired_slots = 16;

		std::filesystem::path retired_name(const std::filesystem::path& target, const int slot)
		{
			auto aside = target;
			aside += L".mwrvr-old." + std::to_wstring(slot);
			return aside;
		}

		// Delete copies an earlier launcher session renamed aside; ones still
		// loaded by a running launcher stay until a later start.
		void remove_retired(const std::filesystem::path& target)
		{
			for (int slot = 0; slot < retired_slots; ++slot)
			{
				std::error_code error;
				std::filesystem::remove(retired_name(target, slot), error);
			}
		}

		// overlord.exe imports d3d11/dxgi, so a launcher started while DXVK was
		// installed has our copies loaded. Windows refuses to delete a loaded
		// image but allows renaming it, which frees the name for the game.
		bool retire(const std::filesystem::path& target)
		{
			std::error_code error;
			if (std::filesystem::remove(target, error) && !error) return true;
			for (int slot = 0; slot < retired_slots; ++slot)
			{
				const auto aside = retired_name(target, slot);
				if (std::filesystem::exists(aside, error)) continue;
				error.clear();
				std::filesystem::rename(target, aside, error);
				if (!error) return true;
			}
			return false;
		}
	}

	void sync(const std::filesystem::path& game_directory, const bool enabled)
	{
		const auto bundle = game_directory / L"overlord-mwr" / L"dxvk";
		std::error_code error;
		for (const auto* name : libraries) remove_retired(game_directory / name);
		// The game loads DXVK from the bundle itself (component/dxvk_runtime.hpp).
		// Copies earlier builds placed in the game folder made every module,
		// including the OpenXR runtime, use DXVK: remove them whatever the option.
		// Files that are not byte-identical to the bundle (ReShade, other wrappers)
		// are never deleted.
		for (const auto* name : libraries)
		{
			const auto source = bundle / name;
			const auto target = game_directory / name;
			if (std::filesystem::exists(target, error) && std::filesystem::exists(source, error) &&
				same_content(source, target) && !retire(target))
				throw std::runtime_error("Could not remove the old " + narrow(target) +
					" (Vulkan renderer). Close the game and try again.");
		}
		if (enabled)
			for (const auto* name : libraries)
				if (!std::filesystem::is_regular_file(bundle / name, error))
					throw std::runtime_error("The Vulkan renderer (DXVK) files are missing: " + narrow(bundle / name) +
						". Reinstall MWR VR or turn the Vulkan renderer off.");
	}
}
