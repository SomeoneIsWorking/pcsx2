// Shared AVP:E guest/host loading-clock capture. Fork-local; not for upstream PCSX2.

#include "AVPE/LoadTimingPoint.h"

#include "Counters.h"
#include "R3000A.h"
#include "R5900.h"

#include <chrono>

namespace AVPE::LoadTimingPoint
{
	Point CaptureNext(u64& ordinal)
	{
		const auto host_time = std::chrono::steady_clock::now().time_since_epoch();
		return {
			.ordinal = ++ordinal,
			.ee_cycle = cpuRegs.cycle,
			.iop_cycle = psxRegs.cycle,
			.frame = g_FrameCount,
			.host_time_ns = static_cast<u64>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(host_time).count()),
		};
	}
} // namespace AVPE::LoadTimingPoint
