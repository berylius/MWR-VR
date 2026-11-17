#include "component/scene_surface_layout.hpp"
#include "component/mwr_scene_surface_storage_contract.hpp"
#include "component/vr/gameplay/skinned_part_visibility.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

int main()
{
	using namespace scene_surface_storage;
	static_assert(capacity==524288 && index_unit==8 && hidden_bytes==8);
	static_assert(native_first_frontend+native_frontend_stride==0x150850e00);
	static_assert(contract::patches.size()==230);
	for(std::size_t id=0;id<65536;++id)
	{
		const auto offset=decode(static_cast<std::uint16_t>(id));
		assert(offset<capacity && encode(offset)==id);
		const auto packed=std::uint64_t{id}<<20;
		assert(((packed>>17)&0x7fff8)==offset);
	}
	assert(!encode(capacity) && !encode(4));
	assert(record_bytes(-3)==8 && record_bytes(0)==56);
	for(int bones=1;bones<=255;++bones)
		assert(record_bytes(-3-bones)==bones*32+56);
	// A mixed packet stream crosses the original 256 KiB boundary and must
	// retain exact record starts after hidden records and brush transforms.
	std::size_t cursor{};
	for(unsigned i=0;i<1000;++i)
		for(const int tag:{-3,-4,0,-7,-3})
		{
			assert(encode(cursor) && decode(*encode(cursor))==cursor);
			cursor+=*record_bytes(tag);
		}
	assert(cursor>262144 && cursor<capacity);
	assert(brush_prefix_bytes%8==0 && brush_surface_bytes/index_unit==3);
	std::vector<std::byte> packets;
	for(const int tag:{-3,-4,0,-7,-3,12})
	{
		const auto start=packets.size();
		packets.resize(start+*record_bytes(tag));
		std::memcpy(packets.data()+start,&tag,4);
	}
	const auto plan=vr::gameplay::weapons::plan_skin_packets(packets,6);
	assert(plan.valid && plan.count==2 && plan.skinned[0]==96 && plan.skinned[1]==344);
	packets.pop_back();
	assert(!vr::gameplay::weapons::plan_skin_packets(packets,6).valid);
	// Preserve two distinct in-flight frontend banks and every compound field.
	for(const std::uintptr_t displacement:{0x10000000,0x20000000})
	{
		for(const auto& patch:contract::patches)
		{
			assert(patch.size<=15 && patch.size>0);
			if(!patch.dynamic)continue;
			std::uint32_t original{};
			std::memcpy(&original,patch.before.data()+patch.field,4);
			const auto value=relocate_operand(patch.dynamic,original,displacement);
			assert(value);
			if(patch.dynamic==1)
				for(unsigned bank=0;bank<2;++bank)
					assert(native_first_frontend+bank*native_frontend_stride+*value==
						native_first_frontend+displacement+bank*native_frontend_stride+original-native_arena_offset);
			if(patch.dynamic==3)
				for(const std::size_t offset:std::array<std::size_t,3>{0,262144,capacity-8})
					assert(((((displacement+offset+*value)<<17)>>20)&65535)==offset/8);
		}
	}
	assert(!relocate_operand(1,native_arena_offset-1,0x10000000));
	assert(!relocate_operand(1,native_arena_offset,0x10000004));
	return 0;
}
