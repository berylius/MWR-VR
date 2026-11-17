#pragma once
#include "component/vr/gameplay/grip_edges.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"

namespace weapon_release_retry_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;
		using namespace carry;
		using vr::hand;
		using namespace std::chrono_literals;
		for (unsigned h = 0; h < 2; ++h)
		{
			const auto actor = static_cast<hand>(h);
			const unsigned bit = 1u << h;
			// Real digital sampler -> production gate -> carry transaction. A wall
			// rejects the release, then the neutral hand moves into clear space.
			inventory bag;
			const owned_weapon item[]{{900, {true, true}}};
			bag.reconcile(item);
			const auto id = bag.find_definition(900)->id;
			bag.equip(id, actor);
			grip_edge_gate gate;
			vr::controller_input::digital_sampler buttons;
			vr::controller_input::frame input;
			input.focused = true;
			input.reference_generation = 1;
			input.sampled_at = vr::controller_input::clock::time_point{} + 1s;
			unsigned attempts{}, drops{}, retries{};
			const auto tick = [&](bool down, bool clear, bool pose = true)
			{
				++input.sequence; input.sampled_at += 10ms;
				input.grip[h].valid = pose;
				input.squeeze[h] = buttons.sample(true, down, input.sampled_at);
				std::array<hold, 2> owners{};
				if (const auto* held = bag.in_hand(actor)) owners[h] = held->owner;
				const auto event = gate.consume(input, true, input.sampled_at, owners);
				if (event.released)
				{
					++attempts;
					if (event.retried) ++retries;
					const auto r = bag.release(id, event.released, location::absent, clear,
					                          [&](const instance&) { ++drops; return true; });
					if (r.action == outcome::rejected)
						check(gate.retry_release(bag.find(id)->owner, event.released) == bit,
						      "rejected placement queues the same retained hand");
				}
				return event;
			};
			tick(true, false);
			check(tick(false, false).released == bit && attempts == 1 && drops == 0 && bag.in_hand(actor),
			      "collision cannot lose the held gun or call native drop");
			for (unsigned n = 0; n < 9; ++n) tick(false, true);
			check(attempts == 1, "blocked release retry is rate limited");
			tick(false, true, false);
			check(attempts == 1, "retry still requires its own hand pose");
			check(tick(false, true).retried == bit && attempts == 2 && drops == 1 && !bag.in_hand(actor),
			      "moving to clear space drops without another grip press");
			for (unsigned n = 0; n < 15; ++n) tick(false, true);
			check(drops == 1 && retries == 1 && bag.invariant(), "successful retry never replays a drop");
		}
		// A retry cannot outlive input continuity, a regrip, or its exact held lease.
		for (unsigned change = 0; change < 9; ++change)
		{
			grip_edge_gate gate;
			vr::controller_input::frame f;
			f.focused = true; f.sequence = f.reference_generation = 1;
			f.sampled_at = vr::controller_input::clock::time_point{} + 1s;
			f.grip[1].valid = true;
			f.squeeze[1] = {true, true, 1, 1, 0};
			std::array<hold, 2> owners{};
			owners[1] = {.weapon = 900, .revision = 1, .rear = hand::right, .instance_generation = 10};
			gate.consume(f, true, f.sampled_at, owners);
			check(gate.retry_release(owners[1], 2) == 0, "holding alone cannot manufacture release intent");
			++f.sequence; f.sampled_at += 10ms; f.squeeze[1].down = false; ++f.squeeze[1].releases;
			check(gate.consume(f, true, f.sampled_at, owners).released == 2, "fixture has a real release");
			check(gate.retry_release(owners[1], 2) == 2, "mechanical preparation rejection can also retry");
			if (change == 0) { f.squeeze[1].down = true; ++f.squeeze[1].presses; }
			if (change == 1) f.focused = false;
			if (change == 2) ++f.squeeze[1].generation;
			if (change == 3) ++f.reference_generation;
			if (change == 4) ++owners[1].instance_generation;
			if (change == 5) ++owners[1].revision;
			if (change == 6) owners[1] = {};
			if (change == 7) gate.latch(hand::right);
			++f.sequence; f.sampled_at += 100ms;
			const auto cancelled = gate.consume(f, change != 8, f.sampled_at, owners);
			check(cancelled.released == 0 && cancelled.retried == 0,
			      "new press, focus, input generation, recenter, ownership, latch or pause cancels retry");
			f.focused = true;
			for (unsigned n = 0; n < 15; ++n)
			{
				++f.sequence; f.sampled_at += 10ms;
				check(gate.consume(f, true, f.sampled_at, owners).released == 0,
				      "returning to gameplay cannot revive a cancelled retry");
			}
		}
		{
			inventory bag;
			const owned_weapon item[]{{900, {true, true}}};
			bag.reconcile(item);
			const auto id = bag.find_definition(900)->id;
			bag.equip(id, hand::right); bag.support(id, hand::left);
			grip_edge_gate gate;
			vr::controller_input::frame f;
			f.focused = true; f.sequence = f.reference_generation = 1;
			f.sampled_at = vr::controller_input::clock::time_point{} + 1s;
			for (unsigned h = 0; h < 2; ++h) { f.grip[h].valid = true; f.squeeze[h] = {true, true, 1, 1, 0}; }
			std::array<hold, 2> owners{bag.find(id)->owner, bag.find(id)->owner};
			gate.consume(f, true, f.sampled_at, owners);
			for (auto& b : f.squeeze) { b.down = false; ++b.releases; }
			++f.sequence; f.sampled_at += 10ms;
			const auto released = gate.consume(f, true, f.sampled_at, owners);
			const auto r = bag.release(id, released.released, location::absent, false, [](const auto&) { return false; });
			check(r.action == outcome::rejected && gate.retry_release(bag.find(id)->owner, released.released) == 2,
			      "simultaneous two-hand rejection retries only the final retained hand");
			owners[0] = {}; owners[1] = bag.find(id)->owner;
			++f.sequence; f.sampled_at += 100ms;
			const auto retry = gate.consume(f, true, f.sampled_at, owners);
			unsigned drops{};
			check(retry.released == 2 && retry.retried == 2 &&
			      bag.release(id, retry.released, location::absent, true, [&](const auto&) { ++drops; return true; }).action == outcome::dropped &&
			      drops == 1 && bag.invariant(), "two-hand retry commits one native drop without transient promotion");
		}
	}
}
