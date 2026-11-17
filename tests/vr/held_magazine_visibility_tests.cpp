#include "component/scene_model_culling.hpp"
#include "component/scene_model_culling_bridge.hpp"
#include <asmjit/core/jitruntime.h>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace
{
	bool requested{};
	unsigned filter(std::uint8_t* banks,unsigned count)
	{return scene_models::preserve_primary_visibility({banks,4},count,requested);}
	struct witness {std::uint64_t registers[7]{},flags{};float lanes[6][4]{};std::uint32_t banks{},rejected{};};
}
int main()
{
	using scene_models::preserve_primary_visibility;
	std::array<std::uint8_t,4> banks{2,2,2,2};
	assert(preserve_primary_visibility(banks,4,false)==4 && banks[0]==2);
	assert(preserve_primary_visibility(banks,4,true)==3 && (banks==std::array<std::uint8_t,4>{1,2,2,2}));
	assert(preserve_primary_visibility(banks,3,true)==3);
	banks={2,1,2,1};assert(preserve_primary_visibility(banks,2,true)==1 && banks[1]==1 && banks[2]==2 && banks[3]==1);
	banks={0,2,2,2};assert(preserve_primary_visibility(banks,3,true)==3 && banks[0]==0);
	banks={2,2,2,2};assert(preserve_primary_visibility(banks,0,true)==0 && banks[0]==2);
	assert(preserve_primary_visibility(std::span(banks).first(3),4,true)==4);

	// Execute the actual bridge with the native stack layout and live registers.
	asmjit::JitRuntime runtime;asmjit::CodeHolder code;code.init(runtime.environment());
	asmjit::x86::Assembler a(&code);float native_constant=17.25f;
	scene_models::emit_primary_visibility_bridge(a,reinterpret_cast<std::uintptr_t>(filter),reinterpret_cast<std::uintptr_t>(&native_constant));
	void* bridge{};assert(runtime.add(&bridge,&code)==asmjit::Error::kOk);
	asmjit::CodeHolder caller;caller.init(runtime.environment());asmjit::x86::Assembler c(&caller);
	using namespace asmjit::x86;
	c.push(rbx);c.mov(rbx,rcx);c.sub(rsp,0xb0); // Original RSP aligned; view banks at +A0.
	c.mov(dword_ptr(rsp,0xa0),0x02020202);c.mov(r11d,4);
	const std::array<Gp,7> registers{rax,rcx,rdx,r8,r9,r10,r11};
	for(unsigned i=0;i<6;++i){c.mov(registers[i],0x12340000+i);c.movd(xmm(i),registers[i].r32());}
	c.stc();c.mov(rax,reinterpret_cast<std::uintptr_t>(bridge));
	// Preserve the caller's test RAX while calling through memory, like the relay.
	c.mov(qword_ptr(rsp,0x80),rax);c.mov(rax,0x12340000);c.call(qword_ptr(rsp,0x80));
	for(unsigned i=0;i<7;++i)c.mov(qword_ptr(rbx,offsetof(witness,registers)+i*8),registers[i]);
	c.pushfq();c.pop(rax);c.mov(qword_ptr(rbx,offsetof(witness,flags)),rax);
	for(unsigned i=0;i<6;++i)c.movdqu(xmmword_ptr(rbx,offsetof(witness,lanes)+i*16),xmm(i));
	c.mov(eax,dword_ptr(rsp,0xa0));c.mov(dword_ptr(rbx,offsetof(witness,banks)),eax);
	c.add(rsp,0xb0);c.pop(rbx);c.ret();
	using invoke=void(*)(witness*);invoke run{};assert(runtime.add(&run,&caller)==asmjit::Error::kOk);
	for(bool enabled:{false,true})
	{
		requested=enabled;witness out;run(&out);
		for(unsigned i=0;i<6;++i)assert(out.registers[i]==0x12340000+i);
		assert(out.registers[6]==(enabled?3:4));assert(out.flags&1);
		assert(out.banks==(enabled?0x02020201u:0x02020202u));
		assert(out.lanes[1][0]==native_constant && out.lanes[1][1]==0 && out.lanes[1][2]==0 && out.lanes[1][3]==0);
		for(unsigned i:{0,2,3,4,5})
		{std::uint32_t value{};std::memcpy(&value,&out.lanes[i][0],4);assert(value==0x12340000+i);}
	}
	std::cout<<"HELD_MAGAZINE_VISIBILITY_OK\n";
}
