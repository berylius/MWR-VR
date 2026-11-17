#include <std_include.hpp>
#include "game_data.hpp"
#include "game/mwr/profile.hpp"
#include <mutex>

namespace game_data
{
	namespace
	{
		const std::filesystem::path private_players = "overlord-mwr/profile/players2";
		std::once_flag profile_ready;

		template <typename Char> class native_path
		{
		public:
			explicit native_path(const Char* input) : original_(input)
			{
				if (!input || !*input) return;
				const std::filesystem::path requested(input);
				const auto mapped = game::mwr::profile::redirect("players2", private_players, requested);
				if (mapped == requested) return;
				if constexpr (std::is_same_v<Char, wchar_t>) value_ = mapped.wstring();
				// Preserve relative ASCII paths when the installation itself has a
				// Unicode name that the game's ANSI API cannot encode.
				else value_ = mapped.lexically_relative(std::filesystem::current_path()).string();
			}
			const Char* get() const { return value_.empty() ? original_ : value_.c_str(); }
		private:
			const Char* original_{};
			std::basic_string<Char> value_;
		};

		// Only the mapped game's imports use these wrappers; the launcher and
		// support code use explicit paths without changing process-wide APIs.
		HANDLE WINAPI create_file_a(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES l, DWORD d, DWORD f, HANDLE t)
		{ try { return CreateFileA(native_path<char>(p).get(), a, s, l, d, f, t); } catch (...) { SetLastError(ERROR_INVALID_NAME); return INVALID_HANDLE_VALUE; } }
		HANDLE WINAPI create_file_w(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES l, DWORD d, DWORD f, HANDLE t)
		{ try { return CreateFileW(native_path<wchar_t>(p).get(), a, s, l, d, f, t); } catch (...) { SetLastError(ERROR_INVALID_NAME); return INVALID_HANDLE_VALUE; } }
		DWORD WINAPI attributes_a(LPCSTR p)
		{ try { return GetFileAttributesA(native_path<char>(p).get()); } catch (...) { SetLastError(ERROR_INVALID_NAME); return INVALID_FILE_ATTRIBUTES; } }
		BOOL WINAPI attributes_ex_w(LPCWSTR p, GET_FILEEX_INFO_LEVELS l, LPVOID v)
		{ try { return GetFileAttributesExW(native_path<wchar_t>(p).get(), l, v); } catch (...) { SetLastError(ERROR_INVALID_NAME); return FALSE; } }
		BOOL WINAPI delete_a(LPCSTR p)
		{ try { return DeleteFileA(native_path<char>(p).get()); } catch (...) { SetLastError(ERROR_INVALID_NAME); return FALSE; } }
		BOOL WINAPI delete_w(LPCWSTR p)
		{ try { return DeleteFileW(native_path<wchar_t>(p).get()); } catch (...) { SetLastError(ERROR_INVALID_NAME); return FALSE; } }
		BOOL WINAPI directory_w(LPCWSTR p, LPSECURITY_ATTRIBUTES l)
		{ try { return CreateDirectoryW(native_path<wchar_t>(p).get(), l); } catch (...) { SetLastError(ERROR_INVALID_NAME); return FALSE; } }
		BOOL WINAPI move_w(LPCWSTR a, LPCWSTR b, DWORD f)
		{ try { return MoveFileExW(native_path<wchar_t>(a).get(), native_path<wchar_t>(b).get(), f); } catch (...) { SetLastError(ERROR_INVALID_NAME); return FALSE; } }
		HANDLE WINAPI find_a(LPCSTR p, FINDEX_INFO_LEVELS l, LPVOID v, FINDEX_SEARCH_OPS s, LPVOID f, DWORD flags)
		{ try { return FindFirstFileExA(native_path<char>(p).get(), l, v, s, f, flags); } catch (...) { SetLastError(ERROR_INVALID_NAME); return INVALID_HANDLE_VALUE; } }
		HANDLE WINAPI find_w(LPCWSTR p, FINDEX_INFO_LEVELS l, LPVOID v, FINDEX_SEARCH_OPS s, LPVOID f, DWORD flags)
		{ try { return FindFirstFileExW(native_path<wchar_t>(p).get(), l, v, s, f, flags); } catch (...) { SetLastError(ERROR_INVALID_NAME); return INVALID_HANDLE_VALUE; } }
	}

	void initialize_players_folder()
	{
		std::call_once(profile_ready, []
		{
			auto source = std::filesystem::path("players2");
			if (!std::filesystem::exists(source / "config.cfg") && std::filesystem::exists(source / "default/config.cfg"))
				source /= "default";
			game::mwr::profile::seed(source, private_players, std::to_string(GetCurrentProcessId()));
		});
	}
	std::string get_config_file_path() { return (private_players / "config.cfg").generic_string(); }
	std::string get_config_source_path()
	{
		if (is_game_directory_available()) initialize_players_folder();
		return get_config_file_path();
	}
	void* resolve_profile_import(const std::string& library, const std::string& name)
	{
		if (_stricmp(library.c_str(), "kernel32.dll") != 0 && _stricmp(library.c_str(), "kernelbase.dll") != 0) return nullptr;
#define PROFILE_IMPORT(api, hook) if (name == #api) return reinterpret_cast<void*>(&hook)
		PROFILE_IMPORT(CreateFileA, create_file_a);
		PROFILE_IMPORT(CreateFileW, create_file_w);
		PROFILE_IMPORT(GetFileAttributesA, attributes_a);
		PROFILE_IMPORT(GetFileAttributesExW, attributes_ex_w);
		PROFILE_IMPORT(DeleteFileA, delete_a);
		PROFILE_IMPORT(DeleteFileW, delete_w);
		PROFILE_IMPORT(CreateDirectoryW, directory_w);
		PROFILE_IMPORT(MoveFileExW, move_w);
		PROFILE_IMPORT(FindFirstFileExA, find_a);
		PROFILE_IMPORT(FindFirstFileExW, find_w);
#undef PROFILE_IMPORT
		return nullptr;
	}
}
