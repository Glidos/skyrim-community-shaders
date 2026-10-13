#include "Features/TextureStreaming/Settings.h"
#include "Utils/GpuMemoryBudget.h"
#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
	void Reject(const nlohmann::json& request)
	{
		try {
			(void)StreamingTextures::ParsePressureSimulation(request);
		} catch (const std::invalid_argument&) {
			return;
		}
		throw std::runtime_error("Malformed pressure request was accepted");
	}
}

int main()
{
	using Memory = Util::GpuMemoryBudget;
	using Simulation = Memory::StreamingPressureSimulation;
	namespace Policy = TextureStreamingPolicy;
	constexpr auto mib = Memory::MiB;
	try {
		Memory::Snapshot physical;
		physical.local.Budget = 16384 * mib;
		physical.local.CurrentUsage = 4096 * mib;
		physical.pendingBytes = 64 * mib;
		physical.recoveryDemandBytes = 256 * mib;
		physical.generation = 7;
		physical.sequence = 1;
		physical.sampledAtMs = 1000;
		physical.result = S_OK;
		Simulation simulation;
		Require(simulation.Configure(physical, 1000, 5000, 512), "Fresh physical budget did not admit a bounded test");
		for (const bool nrPriority : { false, true }) {
			physical.priorityWork = nrPriority;
			const auto effective = simulation.Apply(physical, 1000);
			Require(effective.pressureSimulated && effective.local.Budget == 4672 * mib, "Streaming headroom was not derived from real usage and pending allocations");
			Require(physical.local.Budget == 16384 * mib && effective.physicalBudgetBytes == physical.local.Budget, "Simulation changed physical telemetry");
			Require(effective.priorityWork == nrPriority && effective.pendingBytes == physical.pendingBytes && effective.recoveryDemandBytes == physical.recoveryDemandBytes,
				"Simulation changed NR/render-scale priority, reservations or soft demand");
			Policy::Pressure pressure;
			Require(pressure.Update(1000, 1000, true, effective.local.Budget, Memory::Add(effective.local.CurrentUsage, effective.pendingBytes)) == Policy::PressureChange::Entered,
				"Pressure entry depended on NR priority");
			Require(Memory::StreamingFits(effective.local.Budget, effective.local.CurrentUsage, effective.pendingBytes, 64 * mib, false, false, nrPriority),
				"Safe reclamation was prevented by NR state");
			Require(!Memory::StreamingFits(effective.local.Budget, effective.local.CurrentUsage, effective.pendingBytes, mib, true, false, nrPriority),
				"Optional refill bypassed the simulated reserve");
		}
		physical.priorityWork = false;
		const auto originalBudget = simulation.budgetBytes;
		physical.local.CurrentUsage -= 256 * mib;
		Require(simulation.Apply(physical, 1000).local.Budget == originalBudget, "Reclamation kept moving the simulated budget target");
		physical.local.Budget = 4000 * mib;
		Require(simulation.Apply(physical, 1000).local.Budget == physical.local.Budget, "Simulation concealed genuine lower headroom");
		physical.local.Budget = 16384 * mib;
		Require(!simulation.Apply(physical, 6000).pressureSimulated, "Simulation failed to expire at its deadline");
		Require(!simulation.Apply(physical, 999).pressureSimulated, "Clock rollback kept simulation active");
		++physical.generation;
		Require(!simulation.Apply(physical, 1000).pressureSimulated, "Device replacement reused a pressure test");
		--physical.generation;
		physical.sampledAtMs = 0;
		Require(!simulation.Configure(physical, 1000, 5000, 512) && simulation.budgetBytes == originalBudget, "Invalid telemetry changed the armed test");
		Require(!simulation.Apply(physical, 1000).pressureSimulated, "Stale telemetry was promoted to a simulated valid sample");
		physical.sampledAtMs = 1000;
		Require(!simulation.Configure(physical, 1000, Simulation::MaximumDurationMilliseconds + 1, 512), "Excessive simulation duration was accepted");
		Require(!simulation.Configure(physical, 1000, 5000, Simulation::MaximumHeadroomMiB + 1), "Excessive headroom was accepted");
		Require(simulation.Configure(physical, 1000, 0, 0) && !simulation.expiresAtMs, "Explicit clear retained pressure");
		Require(simulation.Configure(physical, 1000, 1000, 512), "Simulation could not restart after clear");
		Policy::Pressure pressure;
		const auto effective = simulation.Apply(physical, 1000);
		pressure.Update(1000, 1000, true, effective.local.Budget, Memory::Add(effective.local.CurrentUsage, effective.pendingBytes));
		Require(pressure.conserving, "Pressure condition did not enter conservation");
		for (std::uint64_t now = 2000; now <= 2000 + Policy::HealthyWindowMs; now += 250) {
			physical.sampledAtMs = now;
			const auto recovered = simulation.Apply(physical, now);
			Require(!recovered.pressureSimulated && recovered.local.Budget == physical.local.Budget, "Expiry failed to restore physical sampling");
			pressure.Update(now, now, true, recovered.local.Budget, Memory::Add(recovered.local.CurrentUsage, recovered.pendingBytes));
			if (now < 2000 + Policy::HealthyWindowMs)
				Require(pressure.conserving, "Expiry bypassed the production recovery window");
		}
		Require(!pressure.conserving, "Healthy window did not allow recovery");
		Require(Memory::StreamingFits(physical.local.Budget, physical.local.CurrentUsage, physical.pendingBytes, 64 * mib, true, false, false, physical.recoveryDemandBytes),
			"Recovered optional refill remained blocked");
		const auto parsed = StreamingTextures::ParsePressureSimulation({ { "durationMilliseconds", 30000 }, { "freeMiB", 512 } });
		Require(parsed.durationMilliseconds == 30000 && parsed.freeMiB == 512, "Bounded pressure request changed during parsing");
		Require(StreamingTextures::ParsePressureSimulation({ { "durationMilliseconds", 0 } }).durationMilliseconds == 0, "Clear request failed to parse");
		Reject({ { "durationMilliseconds", 1 } });
		Reject({ { "durationMilliseconds", 0 }, { "freeMiB", 0 } });
		Reject({ { "durationMilliseconds", -1 }, { "freeMiB", 512 } });
		Reject({ { "durationMilliseconds", 30001 }, { "freeMiB", 512 } });
		Reject({ { "durationMilliseconds", UINT64_MAX }, { "freeMiB", 512 } });
		Reject({ { "durationMilliseconds", 1.5 }, { "freeMiB", 512 } });
		Reject({ { "durationMilliseconds", 1000 }, { "freeMiB", -1 } });
		Reject({ { "durationMilliseconds", 1000 }, { "freeMiB", 65537 } });
		Reject({ { "durationMilliseconds", 1000 }, { "freeMiB", UINT64_MAX } });
		Reject({ { "durationMilliseconds", 1000 }, { "freeMiB", true } });
		Reject({ { "durationMilliseconds", 1000 }, { "freeMiB", 512 }, { "unknown", 1 } });
		std::cout << "PASS: streaming-only pressure, NR-independent entry, physical safety, reserve admission, expiry, recovery and strict requests\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
