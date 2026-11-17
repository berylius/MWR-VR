#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace scene_models::rigid_visibility
{
	inline constexpr std::size_t capacity=1536,stride=0x88,views=8;
	struct result {std::uint64_t selected{},admitted{},recovered{},failed{},repacked{};};
	// The native preparation job has already resolved entity/PVS visibility and
	// packed every model visible to any view. Restore only selected hand parts
	// in the primary camera; keep shadow banks and failed packing untouched.
	template<class Select,class Prepare>
	result preserve(std::span<std::byte> records,std::span<std::uint8_t> visibility,
		unsigned begin,unsigned end,Select select,Prepare prepare)
	{
		result out;
		if(begin>end || end>capacity || end>records.size()/stride || visibility.size()<views*capacity)return out;
		for(unsigned i=begin;i<end;++i)
		{
			auto* record=records.data()+i*stride;
			if(!select(record))continue;
			++out.selected;
			if(visibility[i]==1){++out.admitted;continue;}
			bool packed{};
			for(unsigned view=1;view<views;++view)packed|=visibility[view*capacity+i]==1;
			if(!packed)
			{
				if(!prepare(record)){++out.failed;continue;}
				++out.repacked;
			}
			visibility[i]=1;++out.admitted;++out.recovered;
		}
		return out;
	}
}
