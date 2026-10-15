#include "Diagnostics/BoundedThreadTrace.h"
#include "Diagnostics/SunReceiverTraceProbe.h"
#include <array>
#include <barrier>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
	void Require(bool value, const char* text)
	{
		if (!value)
			throw std::runtime_error(text);
	}
	void Recorder()
	{
		using Buffer = Diagnostics::BoundedThreadTrace<std::uint64_t, 8, 1024>;
		Buffer buffer;
		buffer.Reset();
		buffer.Open();
		{
			Buffer::Lease first(buffer);
			Require(bool(first), "Initial lease rejected");
			buffer.Close();
			Buffer::Lease late(buffer);
			Require(!late && !buffer.Quiescent(), "Close did not retain existing writer");
			buffer.Append(1, 17);
			bool rejected = false;
			try {
				buffer.Reset();
			} catch (const std::logic_error&) {
				rejected = true;
			}
			Require(rejected, "Reset accepted outstanding writer");
		}
		Require(buffer.Quiescent() && buffer.lanes[0].events[0] == 17, "Closing writer lost its event");
		buffer.Reset();
		buffer.Open();
		std::barrier barrier(8);
		std::vector<std::jthread> workers;
		for (std::uint32_t t = 1; t <= 8; ++t)
			workers.emplace_back([&, t] {
				barrier.arrive_and_wait();
				for (std::uint64_t i = 0; i < 1024; ++i) {
					Buffer::Lease lease(buffer);
					if (lease)
						buffer.Append(t, (std::uint64_t(t) << 32) | i);
				}
			});
		workers.clear();
		buffer.Close();
		Require(buffer.Quiescent() && buffer.unassigned == 0, "Concurrent capture lost a thread");
		for (std::size_t slot = 0; slot < 8; ++slot) {
			const auto& lane = buffer.lanes[slot];
			Require(lane.owner != 0 && lane.count == 1024 && lane.dropped == 0, "Incomplete lane");
			for (std::uint64_t i = 0; i < 1024; ++i)
				Require(lane.events[i] == (std::uint64_t(lane.owner.load()) << 32 | i), "Cross-thread record corruption");
		}
		Diagnostics::BoundedThreadTrace<int, 1, 2> small;
		small.Reset();
		small.Open();
		{
			decltype(small)::Lease lease(small);
			small.Append(1, 3);
			small.Append(1, 5);
			small.Append(1, 7);
			small.Append(2, 9);
		}
		small.Close();
		Require(small.lanes[0].count == 2 && small.lanes[0].dropped == 1 && small.unassigned == 1, "Overflow accounting failed");
		Require(small.lanes[0].events[0] == 3 && small.lanes[0].events[1] == 5, "Overflow overwrote evidence");
	}
	struct MachineState
	{
		std::array<std::uint64_t, 13> words{};
		std::array<std::uint64_t, 12> xmm{};
	};
	struct CallbackState
	{
		std::uint64_t calls{}, wrongAlignment{}, wrongArguments{}, property{}, accumulator{};
	};
	class DestructiveCallback : public Xbyak::CodeGenerator
	{
	public:
		explicit DestructiveCallback(CallbackState& state)
		{
			mov(rax, reinterpret_cast<std::uintptr_t>(&state));
			Xbyak::Label wrongArguments, argumentsChecked;
			cmp(rcx, qword[rax + 24]);
			jne(wrongArguments);
			cmp(rdx, qword[rax + 32]);
			jne(wrongArguments);
			cmp(r8d, 17);
			jne(wrongArguments);
			cmp(r9, qword[rax]);
			jne(wrongArguments);
			jmp(argumentsChecked);
			L(wrongArguments);
			or_(qword[rax + 16], 1);
			L(argumentsChecked);
			inc(qword[rax]);
			mov(rcx, rsp);
			and_(rcx, 15);
			xor_(rcx, 8);
			or_(qword[rax + 8], rcx);
			for (int i = 0; i < 6; ++i)
				pxor(Xbyak::Xmm(i), Xbyak::Xmm(i));
			for (const auto& reg : { rax, rcx, rdx, r8, r9, r10, r11 })
				mov(reg, 0xBADF00D);
			clc();
			ret();
			ready();
		}
	};
	class Harness : public Xbyak::CodeGenerator
	{
	public:
		std::size_t jumpPointer{};
		const std::uint8_t* continuation{};
		Harness(const std::vector<std::uint8_t>& original, bool traced)
		{
			for (const auto& reg : { rbx, rbp, rdi, rsi, r12, r15 })
				push(reg);
			sub(rsp, 0x28);
			mov(rdi, rcx);
			mov(rbp, rcx);
			mov(rsi, rdx);
			mov(rbx, rdx);
			mov(r15, r8);
			xor_(r12d, r12d);
			std::uint64_t seed = 0x12345678;
			for (const auto& reg : { rax, rcx, rdx, r8, r9, r10, r11 })
				mov(reg, seed++);
			for (int i = 0; i < 6; ++i)
				pcmpeqd(Xbyak::Xmm(i), Xbyak::Xmm(i));
			stc();
			if (traced) {
				jmp(ptr[rip]);
				jumpPointer = getSize();
				dq(0);
			} else {
				db(original.data(), original.size());
			}
			continuation = getCurr();
			int offset = 0;
			for (const auto& reg : { rax, rcx, rdx, r8, r9, r10, r11, rdi, rsi, rbp, rbx, r12 }) {
				mov(qword[r15 + offset], reg);
				offset += 8;
			}
			pushfq();
			pop(rax);
			mov(qword[r15 + offset], rax);
			for (int i = 0; i < 6; ++i)
				movdqu(ptr[r15 + 104 + i * 16], Xbyak::Xmm(i));
			add(rsp, 0x28);
			for (const auto& reg : { r15, r12, rsi, rdi, rbp, rbx })
				pop(reg);
			ret();
			ready();
		}
	};
	template <class T>
	void Put(std::byte* destination, std::size_t offset, T value)
	{
		std::memcpy(destination + offset, &value, sizeof(value));
	}
	void Probe()
	{
		using namespace Diagnostics::SunReceiverTrace::Probe;
		for (const bool deferred : { false, true }) {
			const auto& bytes = deferred ? DeferredBytes : ImmediateBytes;
			for (const std::uint32_t marker : { 0u, 1u, 0xFFFFu })
				for (const bool hasLight : { false, true }) {
					std::array<std::byte, 0x80> property{};
					std::array<std::byte, 0x1A0> accumulator{};
					std::array<std::byte, 0x30> light{};
					Put(property.data(), 0x70, hasLight ? light.data() : nullptr);
					Put(accumulator.data(), 0x188, marker);
					Put(accumulator.data(), 0x18C, 8u);
					Harness baseline(bytes, false), observed(bytes, true);
					CallbackState callbackState{ 0, 0, 0, reinterpret_cast<std::uintptr_t>(property.data()), reinterpret_cast<std::uintptr_t>(accumulator.data()) };
					DestructiveCallback callback(callbackState);
					MaskBridge bridge(deferred, 17, bytes, reinterpret_cast<std::uintptr_t>(callback.getCode()),
						reinterpret_cast<std::uintptr_t>(observed.continuation));
					auto entry = reinterpret_cast<std::uintptr_t>(bridge.getCode());
					std::memcpy(const_cast<std::uint8_t*>(observed.getCode()) + observed.jumpPointer, &entry, sizeof(entry));
					using Function = void (*)(void*, void*, MachineState*);
					MachineState expected{}, actual{};
					Put(light.data(), 0x1C, 5u);
					baseline.getCode<Function>()(property.data(), accumulator.data(), &expected);
					std::uint32_t expectedMask{};
					std::memcpy(&expectedMask, light.data() + 0x1C, sizeof(expectedMask));
					Put(light.data(), 0x1C, 5u);
					observed.getCode<Function>()(property.data(), accumulator.data(), &actual);
					std::uint32_t actualMask{};
					std::memcpy(&actualMask, light.data() + 0x1C, sizeof(actualMask));
					Require(callbackState.calls == 2 && callbackState.wrongAlignment == 0 && callbackState.wrongArguments == 0, "Probe callback ABI/alignment mismatch");
					Require(expected.words == actual.words && expected.xmm == actual.xmm, "Probe changed registers, flags or SIMD state");
					Require(actualMask == expectedMask, "Probe changed the native mask update");
					const auto desired = !hasLight || marker == 0 ? 5u : marker == 0xFFFF ? 0u :
					                                                                        13u;
					Require(actualMask == desired, "Native test fixture failed its independent expected mask");
				}
		}
	}
}
int main()
{
	try {
		Recorder();
		Probe();
		std::cout << "PASS: concurrent journal, ownership, overflow and 12 native probe equivalence cases\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
