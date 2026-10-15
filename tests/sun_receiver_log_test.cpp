#include "EngineFixes/SunReceiverLog.h"
#include "EngineFixes/SunReceiverPolicy.h"
#include <atomic>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
	using namespace SunShadowReceiver;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void Concurrent()
	{
		RepairLog log;
		std::atomic_uint32_t built{};
		std::vector<std::jthread> writers;
		for (std::uint32_t worker = 0; worker < 8; ++worker)
			writers.emplace_back([&, worker] {
				for (std::uint32_t i = 0; i < 10000; ++i)
					log.Record([&]() noexcept {
						built.fetch_add(1);
						RepairExample example;
						example.frame = worker * 10000 + i;
						example.maskBefore = 0xA;
						example.maskAfter = 0x4A;
						return example;
					});
			});
		writers.clear();
		Require(log.Total() == 80000 && built == MaximumLogExamples, "count must be exact and example work capped under concurrency");
		std::size_t summaries = 0, examples = 0;
		auto summary = [&](std::uint64_t delta, std::uint64_t total) {
			Require(delta == 80000 && total == 80000, "first summary must retain all successful repairs");
			++summaries;
		};
		auto example = [&](std::size_t ordinal, const RepairExample& value) {
			Require(ordinal == examples + 1 && value.maskBefore == 0xA && value.maskAfter == 0x4A, "published examples must remain complete");
			++examples;
		};
		log.Report(summary, example);
		log.Report(summary, example);
		Require(summaries == 1 && examples == MaximumLogExamples, "idle reporting cannot flood the log or repeat examples");
	}
	void DelayedPublication()
	{
		RepairLog log;
		std::binary_semaphore entered(0), finish(0);
		std::jthread writer([&] {
			log.Record([&]() noexcept {
				entered.release();
				finish.acquire();
				RepairExample example;
				example.frame = 42;
				return example;
			});
		});
		entered.acquire();
		std::size_t summaries = 0, examples = 0;
		auto summary = [&](std::uint64_t delta, std::uint64_t total) {
			Require(delta == 1 && total == 1, "count can publish before its example");
			++summaries;
		};
		auto example = [&](std::size_t, const RepairExample& value) {
			Require(value.frame == 42, "reader cannot observe a partially built example");
			++examples;
		};
		log.Report(summary, example);
		Require(examples == 0, "unfinished example must not block or leak to the reporter");
		finish.release();
		writer.join();
		log.Report(summary, example);
		Require(summaries == 1 && examples == 1, "late example must publish even if repair total is unchanged");
	}
	void RetryAndEligibility()
	{
		RepairLog log;
		Policy::Input input;
		input.primaryPass = input.available = input.identityStable = input.shadowsEnabled = true;
		input.lightData = 1;
		input.flags = Policy::ReceiveShadows;
		input.sunMask = 0x40;
		input.activeMask = 0xA;
		input.shadowDistance = 100;
		std::uint32_t mask = input.activeMask;
		for (bool repair : { false, true, true })
			if (Policy::Apply(mask, 1, input, Policy::Evaluate(input), repair))
				log.Record([]() noexcept { return RepairExample{}; });
		Require(log.Total() == 1, "observe and stale repair attempts must not inflate the ordinary log");
		try {
			log.Report([](auto, auto) { throw std::runtime_error("sink failure"); }, [](auto, const auto&) {});
		} catch (const std::runtime_error&) {}
		std::uint64_t reported = 0;
		std::size_t examples = 0;
		try {
			log.Report([&](auto delta, auto total) { Require(delta == 1, "failed summary must retry its full delta"); reported = total; },
				[](auto, const auto&) { throw std::runtime_error("example failure"); });
		} catch (const std::runtime_error&) {}
		log.Report([](auto, auto) { throw std::runtime_error("successful summary must not repeat"); },
			[&](auto, const auto&) { ++examples; });
		Require(reported == 1 && examples == 1, "failed example must remain available for retry");
	}
}
int main()
{
	try {
		Concurrent();
		DelayedPublication();
		RetryAndEligibility();
		std::cout << "PASS: repair-only counts, 8 writers, four immutable examples, delayed publication, idle suppression and retry; storage="
				  << sizeof(SunShadowReceiver::RepairLog) << " bytes\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
