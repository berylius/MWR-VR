#pragma once
#include <cstdint>

namespace vr::gameplay::weapons::carry
{
	// GetUseList filters a manual pickup before Touch_Item ever sees the grip.
	// Both boundaries need the same admission; an automatic/native query must
	// retain definition-based scavenging and the engine's script restrictions.
	enum class pickup_context { native, hand_query, grip_transfer };
	inline constexpr bool admit_duplicate_pickup(pickup_context context,bool local_live_item,
		bool supported_owned_weapon,int automatic,int dual,std::uint32_t script_flags) noexcept
	{
		return (context==pickup_context::hand_query || context==pickup_context::grip_transfer) &&
			local_live_item && supported_owned_weapon && automatic==0 && dual==0 && !(script_flags&0x8080u);
	}
	// Both explicit use and automatic ammo scavenging reach the same native
	// item-touch body. Only the exact grip transaction may consume a firearm by
	// use. A walk-over (automatic) touch takes the native ammo-only path
	// (MWR 0x1402A0C00: owned or ammo-sharing weapon -> stock, never a weapon
	// grant), as in the stock game, except for guns the player dropped.
	inline constexpr bool suppress_weapon_touch(bool physical_carry, bool local_player,
		bool firearm_item, bool admitted_item, bool automatic=false, bool player_dropped=false) noexcept
	{
		return physical_carry && local_player && firearm_item && !admitted_item && !(automatic && !player_dropped);
	}
}
