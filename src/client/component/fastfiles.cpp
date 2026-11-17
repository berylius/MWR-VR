#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "fastfiles.hpp"
#include <utils/hook.hpp>

// MWR owns zone retirement and asset enumeration. H2 pool expansion, custom
// zone loading and H2 filesystem ABIs are deliberately outside this host.
namespace fastfiles
{
	namespace { std::vector<void(*)()> pre_unload_callbacks; }
	void on_pre_unload(void (*callback)())
	{
		if (callback) pre_unload_callbacks.push_back(callback);
	}
	void enum_assets(const game::XAssetType type, const std::function<void(game::XAssetHeader)>& callback, bool include_override)
	{
		game::DB_EnumXAssets_Internal(type, [](game::XAssetHeader header, void* data)
		{
			(*static_cast<const std::function<void(game::XAssetHeader)>*>(data))(header);
		}, &callback, include_override);
	}
}

namespace fastfiles::mwr_unload
{
	namespace
	{
		utils::hook::detour unload_hook;
		void unload_stub(const unsigned short* zones, unsigned int count, bool create_default)
		{
			if (count) for (const auto callback : pre_unload_callbacks) callback();
			unload_hook.invoke<void>(zones, count, create_default);
		}
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			constexpr std::uint8_t prologue[]{0x40,0x53,0x55,0x56,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x28};
			if (std::memcmp(reinterpret_cast<const void*>(0x1401F6040), prologue, sizeof(prologue)))
				throw std::runtime_error("MWR DB_UnloadXZones prologue rejected");
			unload_hook.create(0x1401F6040, unload_stub);
		}
	};
}
REGISTER_COMPONENT(fastfiles::mwr_unload::component)
