#include "component/scene_dobj_visibility.hpp"
#include "component/scene_rigid_visibility.hpp"
#include <array>
#include <cassert>
#include <iostream>

int main()
{
	using namespace scene_models::dobj_visibility;
	std::array<std::byte,capacity*stride> records{};
	std::array<std::uint8_t,capacity> visibility{};
	int left{},right{},foreign{};
	const auto entry=[&](unsigned i,unsigned entity,const void* object,unsigned state)
	{
		auto* p=records.data()+i*stride;entity=(entity<<8)|0x5a;
		std::memcpy(p+0x90,&entity,4);std::memcpy(p+0x98,&object,sizeof(object));std::memcpy(p+0x48,&state,4);
	};
	entry(0,3999,&right,5);entry(1,3998,&left,7);
	entry(2,4000,&left,5); // Same object in a storage/world submission stays culled.
	entry(3,3999,&foreign,5);entry(4,3999,&right,4); // Packing failure stays hidden.
	entry(5,3999,nullptr,5);entry(6,3999,&right,0);
	entry(7,3999,&right,2); // Incomplete native preparation must not become visible.
	entry(511,3998,&left,5);
	unsigned preparations{};bool owned=true;
	const auto select=[&](const void* object){return owned && (object==&left || object==&right);};
	const auto prepare=[&](std::byte* record)
	{
		++preparations;
		if(record==records.data()+6*stride){unsigned ready=6;std::memcpy(record+0x48,&ready,4);}
	};
	visibility[1]=1;
	const auto original=records;
	auto r=preserve(records,visibility,0,8,select,prepare);
	assert(r.selected==5 && r.admitted==3 && r.recovered==2 && r.failed==2 && preparations==5);
	assert(visibility[0]==1 && visibility[1]==1 && visibility[6]==1);
	for(unsigned i:{2,3,4,5,7,511})assert(visibility[i]==0);
	// The policy changes no shared model, transform, hide bits or shadow bank.
	for(unsigned i=0;i<records.size();++i)if(i<6*stride+0x48 || i>=6*stride+0x4c)assert(records[i]==original[i]);
	const auto before=visibility;owned=false;
	r=preserve(records,visibility,0,8,select,prepare);
	assert(!r.selected && preparations==5 && visibility==before);
	owned=true;
	r=preserve(records,visibility,511,512,select,prepare);
	assert(r.admitted==1 && visibility[511]==1);
	assert(!preserve(records,visibility,512,513,select,prepare).selected);
	assert(!preserve(records,visibility,8,0,select,prepare).selected);
	assert(!preserve(std::span(records).first(stride),visibility,0,2,select,prepare).selected);
	assert(!preserve(records,std::span(visibility).first(1),0,2,select,prepare).selected);
	{
		namespace rigid=scene_models::rigid_visibility;
		std::array<std::byte,rigid::capacity*rigid::stride> rigid_records{};
		std::array<std::uint8_t,rigid::capacity*rigid::views> banks{};
		banks.fill(2);
		unsigned calls{};
		const auto selected=[&](const void* record)
		{
			const auto i=(static_cast<const std::byte*>(record)-rigid_records.data())/rigid::stride;
			return i!=3; // Foreign world prop / released magazine.
		};
		const auto pack=[&](const void* record){++calls;return record!=rigid_records.data()+rigid::stride;};
		banks[2+rigid::capacity]=1; // Shadow already packed this receiver.
		banks[4]=1; // Already visible chamber cartridge.
		banks[1]=0; // Native packing failed; must not expose its stale payload.
		const auto old_banks=banks;
		auto result=rigid::preserve(rigid_records,banks,0,5,selected,pack);
		assert(result.selected==4 && result.admitted==3 && result.recovered==2 && result.failed==1 && result.repacked==1 && calls==2);
		assert(banks[0]==1 && banks[1]==0 && banks[2]==1 && banks[3]==2 && banks[4]==1);
		for(unsigned i=5;i<banks.size();++i)assert(banks[i]==old_banks[i]);
		result=rigid::preserve(rigid_records,banks,1535,1536,selected,pack);
		assert(result.recovered==1 && banks[1535]==1 && calls==3);
		assert(!rigid::preserve(rigid_records,banks,1536,1537,selected,pack).selected);
		assert(!rigid::preserve(rigid_records,banks,5,0,selected,pack).selected);
		assert(!rigid::preserve(std::span(rigid_records).first(rigid::stride),banks,0,2,selected,pack).selected);
		assert(!rigid::preserve(rigid_records,std::span(banks).first(rigid::capacity),0,2,selected,pack).selected);
		std::cout<<"HELD_RIGID_PART_VISIBILITY_OK\n";
	}
	std::cout<<"HELD_WEAPON_VISIBILITY_OK\n";
}
