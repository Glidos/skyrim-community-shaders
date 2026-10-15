#pragma once

#include <cstdint>
#include <vector>
#include <xbyak/xbyak.h>

namespace Diagnostics::SunReceiverTrace::Probe
{
	class MaskBridge final : public Xbyak::CodeGenerator
	{
		void Observe(bool deferred, std::uint32_t site, std::uint32_t after, std::uintptr_t callback)
		{
			pushfq();
			for (const auto& reg : { rax, rcx, rdx, r8, r9, r10, r11 })
				push(reg);
			sub(rsp, 0x80);
			for (int i = 0; i < 6; ++i)
				movdqu(ptr[rsp + 0x20 + i * 16], Xbyak::Xmm(i));
			mov(rcx, deferred ? rbp : rdi);
			mov(rdx, deferred ? rbx : rsi);
			mov(r8d, site);
			mov(r9d, after);
			mov(rax, callback);
			call(rax);
			for (int i = 0; i < 6; ++i)
				movdqu(Xbyak::Xmm(i), ptr[rsp + 0x20 + i * 16]);
			add(rsp, 0x80);
			for (const auto& reg : { r11, r10, r9, r8, rdx, rcx, rax })
				pop(reg);
			popfq();
		}

	public:
		MaskBridge(bool deferred, std::uint32_t site, const std::vector<std::uint8_t>& bytes, std::uintptr_t callback, std::uintptr_t continuation)
		{
			Observe(deferred, site, 0, callback);
			// All copied branches stay within this block or land immediately after it.
			db(bytes.data(), bytes.size());
			Observe(deferred, site, 1, callback);
			jmp(ptr[rip]);
			dq(continuation);
			ready();
		}
	};
	inline const std::vector<std::uint8_t> ImmediateBytes{
		0x8B, 0x86, 0x8C, 0x01, 0x00, 0x00, 0x8B, 0x96, 0x88, 0x01, 0x00, 0x00, 0x85, 0xD2, 0x74, 0x1D,
		0x48, 0x8B, 0x4F, 0x70, 0x48, 0x85, 0xC9, 0x74, 0x14, 0x81, 0xFA, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x09,
		0xC7, 0x41, 0x1C, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x03, 0x09, 0x41, 0x1C
	};
	inline const std::vector<std::uint8_t> DeferredBytes{
		0x8B, 0x8B, 0x88, 0x01, 0x00, 0x00, 0x8B, 0x93, 0x8C, 0x01, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x1A,
		0x48, 0x8B, 0x45, 0x70, 0x48, 0x85, 0xC0, 0x74, 0x11, 0x81, 0xF9, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x06,
		0x44, 0x89, 0x60, 0x1C, 0xEB, 0x03, 0x09, 0x50, 0x1C
	};
}
