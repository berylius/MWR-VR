#pragma once
#include <filesystem>
#include <string>
#include <stdexcept>
#include <vector>
#include <cwctype>

namespace game::mwr::profile
{
	namespace fs = std::filesystem;
	inline bool selected(const fs::path& name)
	{
		const auto file = name.filename().wstring();
		return file == L"config.cfg" || file == L"keys.cfg" || file == L"savegame.svg" ||
			file == L"savegame.svg.bak" || file == L"settings_c.zip.h1" || file == L"settings_s.zip.h1" ||
			(file.starts_with(L"h1_") && name.extension() == ".stat");
	}
	// An existing private profile wins. Commit the complete first import at once.
	inline void seed(const fs::path& source, const fs::path& destination, const std::string& nonce)
	{
		if (fs::exists(destination) && !fs::is_empty(destination)) return;
		fs::create_directories(destination.parent_path());
		fs::path staging = destination;
		staging += ".import-" + nonce;
		if (!fs::create_directory(staging)) throw std::runtime_error("Private profile import is already active");
		std::vector<fs::path> copied;
		try
		{
			std::uintmax_t total{};
			if (fs::is_directory(source))
				for (const auto& entry : fs::directory_iterator(source))
				{
					if (!selected(entry.path())) continue;
					if (entry.is_symlink() || !entry.is_regular_file())
						throw std::runtime_error("SP profile contains an unsupported file");
					const auto size = entry.file_size();
					if (size > 64 * 1024 * 1024 || total + size > 256 * 1024 * 1024)
						throw std::runtime_error("SP profile import is too large");
					total += size;
					const auto output = staging / entry.path().filename();
					copied.push_back(output);
					fs::copy_file(entry.path(), output);
					fs::last_write_time(output, entry.last_write_time());
				}
			if (fs::exists(destination))
			{
				if (!fs::is_empty(destination)) throw std::runtime_error("Private profile changed during import");
				fs::remove(destination);
			}
			fs::rename(staging, destination);
		}
		catch (...)
		{
			std::error_code ignored;
			for (const auto& path : copied) fs::remove(path, ignored);
			fs::remove(staging, ignored);
			throw;
		}
	}
	inline fs::path redirect(const fs::path& source, const fs::path& destination, const fs::path& requested)
	{
		const auto from = fs::absolute(source).lexically_normal();
		const auto input = fs::absolute(requested).lexically_normal();
		auto root = from.begin(), part = input.begin();
		for (; root != from.end(); ++root, ++part)
		{
			if (part == input.end()) return requested;
			auto a = root->wstring(), b = part->wstring();
			for (auto& c : a) c = static_cast<wchar_t>(std::towlower(c));
			for (auto& c : b) c = static_cast<wchar_t>(std::towlower(c));
			if (a != b) return requested;
		}
		auto result = fs::absolute(destination).lexically_normal();
		for (; part != input.end(); ++part) result /= *part;
		return result;
	}
}
