#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace scene_models::dobj_visibility
{
	inline constexpr std::size_t capacity=512, stride=0xb0;
	struct result { std::uint64_t selected{}, admitted{}, recovered{}, failed{}; };
	// MWR scene DObj: preparation state +0x48, entity +0x90, object +0x98.
	// Run on the native preparation job's own range before its surface consumer.
	template<class Select,class Prepare>
	result preserve(std::span<std::byte> records,std::span<std::uint8_t> visibility,
		unsigned begin,unsigned end,Select select,Prepare prepare)
	{
		result out;
		if(begin>end || end>capacity || end>visibility.size() || end>records.size()/stride)return out;
		for(unsigned i=begin;i<end;++i)
		{
			auto* record=records.data()+i*stride;
			std::uint32_t entity{};const void* object{};
			std::memcpy(&entity,record+0x90,sizeof(entity));
			// Only the two hand viewmodel identities; storage uses 4000 and up.
			entity=(entity>>8)&0xfff;
			if(entity!=3998 && entity!=3999)continue;
			std::memcpy(&object,record+0x98,sizeof(object));
			if(!object || !select(object))continue;
			++out.selected;
			prepare(record); // Native state machine synchronizes/reuses completed skinning.
			std::uint32_t state{};std::memcpy(&state,record+0x48,sizeof(state));
			// 0..3 = unprepared/in progress, 4 = failed, 5+ = packed surfaces.
			if(state<5){++out.failed;continue;}
			++out.admitted;if(visibility[i]!=1)++out.recovered;
			visibility[i]=1;
		}
		return out;
	}
}
