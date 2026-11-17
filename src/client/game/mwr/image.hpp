#pragma once
#include <cstdint>

namespace game::mwr::image
{
	inline constexpr std::uintptr_t base = 0x140000000;
	inline constexpr std::uint32_t size = 0x12654800;
	inline constexpr std::uint32_t timestamp = 0x5D374DBA;
	inline constexpr std::uintmax_t file_size = 0x1346D88;
	inline constexpr std::uint32_t version_probe_rva = 0x123456;
	inline constexpr std::uint32_t version_probe_value = 0xD0BC8AA6;
	constexpr bool contains(std::uintptr_t address) noexcept { return address >= base && address - base < size; }
}
