#pragma once
#include "../controller_input.hpp"
#include "weapon_holding.hpp"

namespace vr::gameplay::weapons::carry
{
	struct grip_edges { unsigned pressed{}, released{}, deferred{}, resumed{}, retried{}, consumed{}; };
	// Body-return equipment needs no world placement pose. An active, fresh
	// neutral button can settle its held relation even if an edge was lost.
	inline unsigned neutral_grips(const controller_input::frame& input,controller_input::clock::time_point now)noexcept
	{
		if(!input.focused || !input.sequence || now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150))return 0;
		unsigned result{};for(unsigned h=0;h<2;++h)if(input.squeeze[h].active && !input.squeeze[h].down)result|=1u<<h;return result;
	}
	class grip_edge_gate
	{
	public:
		grip_edges consume(const controller_input::frame& input,bool gameplay,controller_input::clock::time_point now,
		                   const std::array<hold,2>& owners={}, unsigned toggle_mask=0) noexcept
		{
			grip_edges out;
			const bool fresh=gameplay && input.focused && input.sequence && now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150);
			for (std::size_t h=0;h<2;++h)
			{
				auto& s=hands_[h];const auto& button=input.squeeze[h];
				s.delivered=false;
				const auto toggle=(toggle_mask&(1u<<h)) ? owners[h].id() : weapon_identity{};
				const bool valid=fresh && button.active;
				const bool pose=input.grip[h].valid;
				if (!valid || !s.seen || button.generation!=s.button.generation || input.reference_generation!=s.reference ||
					button.presses<s.button.presses || button.releases<s.button.releases ||
					input.sequence<s.sequence || now<s.at)
				{
					// Do not acquire on resume. A held button can still authorize its
					// NEXT real release, even if its original press was not observed.
					s={button,input.reference_generation,input.sequence,now,valid,valid && button.down && !toggle,false};
					s.toggle=toggle;
					if (s.armed) out.resumed|=1u<<h;
					continue;
				}
				if(toggle!=s.toggle)
				{s.pending=s.armed=false;s.retry_id={};s.toggle=toggle;}
				if (button.presses>s.button.presses)
				{
					s.pending=bool(toggle);s.armed=!toggle;s.retry_id={};
					s.toggle_revision=owners[h].revision;
					if(toggle)out.consumed|=1u<<h;
					else if (pose) out.pressed|=1u<<h;
				}
				if (!toggle && button.releases>s.button.releases && s.armed)
				{s.pending=true;s.armed=false;}
				// Button counters survive a missing pose. Retain a proven release
				// until this hand has geometry; pose loss alone never releases it.
				if (s.retry_id && ((!s.retry_toggle && button.down) || owners[h].id()!=s.retry_id || owners[h].revision!=s.retry_revision))
				{s.pending=false;s.retry_id={};}
				if(s.pending && toggle && owners[h].revision!=s.toggle_revision)
				{s.pending=false;s.retry_id={};}
				if (s.pending)
				{
					if(toggle)out.consumed|=1u<<h;
					if (pose && (!s.retry_id || now>=s.retry_after))
					{
						out.released|=1u<<h;s.pending=false;s.delivered=true;
						s.delivered_toggle=bool(toggle);
						if(s.retry_id)out.retried|=1u<<h;
						s.retry_id={};
					}
					else out.deferred|=1u<<h;
				}
				s.button=button;s.reference=input.reference_generation;s.sequence=input.sequence;s.at=now;
			}
			return out;
		}
		// A placement/native transaction may reject a real release. Keep that
		// intent only for the retained grip, never an exchanged/reacquired gun.
		unsigned retry_release(const hold& owner,unsigned released) noexcept
		{
			unsigned queued{};
			for(unsigned h=0;h<2;++h)
			{
				auto& s=hands_[h];const auto actor=static_cast<hand>(h);
				if(!(released&(1u<<h)) || !s.delivered || !s.seen || (s.button.down && !s.delivered_toggle) ||
				   !owner.id() || (owner.rear!=actor && owner.support!=actor))continue;
				s.pending=true;s.retry_id=owner.id();s.retry_revision=owner.revision;
				s.retry_toggle=s.delivered_toggle;
				s.retry_after=s.at+std::chrono::milliseconds(100);queued|=1u<<h;
			}
			return queued;
		}
		// A delayed pickup's currently held grip must retain
		// the next physical release rather than require a second squeeze.
		void adopt(hand h,const controller_input::frame& input,controller_input::clock::time_point now)noexcept
		{
			if(!valid_hand(h))return;const auto& button=input.squeeze[static_cast<unsigned>(h)];
			const bool valid=input.focused && button.active && input.sequence && now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150);
			hands_[static_cast<unsigned>(h)]={button,input.reference_generation,input.sequence,now,valid,valid && button.down,false};
		}
		// An exchanged weapon remains held after the release which acquired it.
		void latch(hand h) noexcept
		{
			if (valid_hand(h)) {auto& s=hands_[static_cast<int>(h)];s.armed=s.pending=s.delivered=false;s.retry_id={};}
		}
	private:
		struct state
		{
			controller_input::digital_action button{};std::uint64_t reference{},sequence{};
			controller_input::clock::time_point at{};bool seen{},armed{},pending{},delivered{};
			weapon_identity retry_id{};std::uint64_t retry_revision{};controller_input::clock::time_point retry_after{};
			weapon_identity toggle{};std::uint64_t toggle_revision{};bool delivered_toggle{},retry_toggle{};
		};
		std::array<state,2> hands_{};
	};
}
