#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <filesystem>
#include <stdexcept>
#include <string>

// MWR: optional Vulkan renderer (vr_dxvk). DXVK is loaded by full path from
// overlord-mwr/dxvk and handed only to the game's own CreateDXGIFactory /
// D3D11CreateDevice imports (component/d3d11.cpp). The process-wide names
// d3d11.dll/dxgi.dll stay the system ones, so in-process OpenXR runtimes that
// create their own D3D11 device for Vulkan interop (Virtual Desktop's VDXR:
// shared ID3D11Fence -> VkSemaphore) get real D3D11. With DXVK as the game
// folder's d3d11.dll that fence failed and xrCreateSession returned
// XR_ERROR_RUNTIME_FAILURE.
namespace dxvk_runtime
{
	struct entry_points
	{
		decltype(&CreateDXGIFactory1) create_factory1{};
		decltype(&D3D11CreateDevice) create_device{};
	};

	namespace detail
	{
		inline entry_points& state()
		{
			static entry_points value{};
			return value;
		}

		inline std::filesystem::path bundle_directory()
		{
			std::wstring path(MAX_PATH, L'\0');
			for (;;)
			{
				const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
				if (length == 0) throw std::runtime_error("Could not locate overlord.exe for the Vulkan renderer (DXVK).");
				if (length < path.size()) { path.resize(length); break; }
				path.resize(path.size() * 2);
			}
			return std::filesystem::path(path).parent_path() / L"overlord-mwr" / L"dxvk";
		}

		// DXVK d3d11 imports CreateDXGIFactory1 by the name dxgi.dll, which the
		// loader binds to the already loaded system dxgi. Point that one slot at
		// DXVK's dxgi (an adapter-less D3D11CreateDevice needs a DXVK factory).
		inline bool bind_factory_import(HMODULE module, void* target)
		{
			const auto base = reinterpret_cast<std::uint8_t*>(module);
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!directory.VirtualAddress) return false;
			for (auto* entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); entry->Name; ++entry)
			{
				if (_stricmp(reinterpret_cast<const char*>(base + entry->Name), "dxgi.dll") != 0) continue;
				auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + (entry->OriginalFirstThunk ? entry->OriginalFirstThunk : entry->FirstThunk));
				auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + entry->FirstThunk);
				for (; names->u1.AddressOfData; ++names, ++slots)
				{
					if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
					const auto* name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
					if (std::strcmp(reinterpret_cast<const char*>(name->Name), "CreateDXGIFactory1") != 0) continue;
					DWORD protect{};
					if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &protect)) return false;
					slots->u1.Function = reinterpret_cast<ULONG_PTR>(target);
					VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), protect, &protect);
					return true;
				}
			}
			return false;
		}

		inline HMODULE load_side_by_side(const std::filesystem::path& file)
		{
			if (!std::filesystem::is_regular_file(file))
				throw std::runtime_error("The Vulkan renderer (DXVK) files are missing from overlord-mwr\\dxvk. "
					"Reinstall MWR VR or turn the Vulkan renderer off.");
			const auto module = LoadLibraryExW(file.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (!module)
				throw std::runtime_error("Could not load the Vulkan renderer (DXVK) error " + std::to_string(GetLastError()) +
					". Turn the Vulkan renderer off in the launcher.");
			// A full-path load must map a second module, never return the system one.
			wchar_t loaded[MAX_PATH]{};
			std::error_code error;
			if (!GetModuleFileNameW(module, loaded, MAX_PATH) ||
				!std::filesystem::equivalent(std::filesystem::path(loaded), file, error))
				throw std::runtime_error("The Vulkan renderer (DXVK) could not load beside the system DirectX DLLs.");
			return module;
		}
	}

	// Called once at startup, before the game binary's imports are resolved.
	// OVERLORD_MWR_DXVK=0/1 overrides the saved setting (mock tests).
	inline void select(bool enabled)
	{
		if (wchar_t value[8]{}; GetEnvironmentVariableW(L"OVERLORD_MWR_DXVK", value, 8) == 1 &&
			(value[0] == L'0' || value[0] == L'1'))
			enabled = value[0] == L'1';
		if (!enabled || detail::state().create_device) return;
		// OpenXR's xrWaitFrame paces VR frames. DXVK must not add its own pacing
		// on the desktop mirror swap chain: no latency sleep (NV low latency),
		// no frame-rate limiter and no forced v-sync. Options the user already set
		// in DXVK_CONFIG come later and win.
		{
			std::wstring config = L"dxvk.latencySleep = False; dxgi.maxFrameRate = 0; dxgi.syncInterval = 0";
			wchar_t existing[1024]{};
			if (const auto length = GetEnvironmentVariableW(L"DXVK_CONFIG", existing, 1024); length > 0 && length < 1024)
				config += L"; " + std::wstring(existing, length);
			SetEnvironmentVariableW(L"DXVK_CONFIG", config.c_str());
		}
		const auto directory = detail::bundle_directory();
		const auto dxgi = detail::load_side_by_side(directory / L"dxgi.dll");
		const auto d3d11 = detail::load_side_by_side(directory / L"d3d11.dll");
		entry_points points{};
		points.create_factory1 = reinterpret_cast<decltype(points.create_factory1)>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
		points.create_device = reinterpret_cast<decltype(points.create_device)>(GetProcAddress(d3d11, "D3D11CreateDevice"));
		if (!points.create_factory1 || !points.create_device ||
			!detail::bind_factory_import(d3d11, reinterpret_cast<void*>(points.create_factory1)))
			throw std::runtime_error("The bundled Vulkan renderer (DXVK) is incomplete. Reinstall MWR VR or turn it off.");
		detail::state() = points;
	}

	// Null when the game uses the system Direct3D 11.
	inline const entry_points* active()
	{
		return detail::state().create_device ? &detail::state() : nullptr;
	}
}
