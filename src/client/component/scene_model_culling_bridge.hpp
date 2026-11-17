#pragma once
#include <asmjit/x86/x86assembler.h>
#include <cstdint>

namespace scene_models
{
	// Replaces the eight-byte MOVSS at MWR 0x14053BBFB with CALL + NOPs.
	// The native frustum loop is complete: four result bytes at original RSP+A0,
	// rejected-view count in R11D, original RSP aligned to 16 before CALL.
	inline void emit_primary_visibility_bridge(asmjit::x86::Assembler& a,std::uintptr_t filter,std::uintptr_t native_constant)
	{
		using namespace asmjit::x86;
			a.pushfq();
			a.push(rax);a.push(rcx);a.push(rdx);a.push(r8);a.push(r9);a.push(r10);a.push(r11);
			a.sub(rsp,0x88);
			for(unsigned i=0;i<6;++i)a.movdqu(xmmword_ptr(rsp,0x20+i*16),asmjit::x86::xmm(i));
			a.lea(rcx,ptr(rsp,0x170)); // Native RSP+A0, accounting for CALL and saves.
			a.mov(edx,r11d);
			a.mov(rax,filter);a.call(rax);
			a.mov(eax,eax);a.mov(qword_ptr(rsp,0x88),rax); // Replace saved R11 only.
			for(unsigned i=0;i<6;++i)a.movdqu(asmjit::x86::xmm(i),xmmword_ptr(rsp,0x20+i*16));
			a.add(rsp,0x88);
			a.pop(r11);a.pop(r10);a.pop(r9);a.pop(r8);a.pop(rdx);a.pop(rcx);a.pop(rax);a.popfq();
			// Replay MOVSS xmm1,[native constant], preserving all other registers.
			a.push(rax);a.mov(rax,native_constant);
			a.movss(xmm1,dword_ptr(rax));a.pop(rax);a.ret();
	}
}
