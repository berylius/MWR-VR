#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include <utils/hook.hpp>

// MWR only (no H2 equivalent). The game's libcurl/hardware-survey allocator
// (0x140510FF0, lock 0x8A) carves its heap 0x14DA5C7E0 out of a static 512 KB
// arena. "Yes" on the startup "hardware changed" dialog serializes a hardware
// report through it; under the Vulkan renderer (DXVK) that report no longer
// fits and the game stops with "Out of memory error (12)" (0x14051103D ->
// 0x140457B80). The arena is replaced with a larger one before the heap is
// built on it; the allocator only uses the arena's own start/end fields.
namespace survey_heap
{
	namespace
	{
		constexpr std::size_t arena_size = 16 * 1024 * 1024;

		// Arena_Create(arena, base, size) 0x14041D850 called at 0x1405111E7.
		void create_arena_stub(void** arena, void* /*static_base*/, std::uint32_t /*static_size*/)
		{
			static auto* const base = static_cast<std::uint8_t*>(
				VirtualAlloc(nullptr, arena_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (!base) throw std::runtime_error("Could not reserve the hardware survey heap.");
			arena[0] = base;
			arena[1] = base + arena_size;
			arena[2] = nullptr;
			arena[3] = nullptr;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			utils::hook::call(0x1405111E7, create_arena_stub);
		}
	};
}

REGISTER_COMPONENT(survey_heap::component)
