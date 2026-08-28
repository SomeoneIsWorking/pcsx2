// Shared AVP:E guest/host loading-clock capture. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE::LoadTimingPoint
{
	struct Point
	{
		u64 ordinal = 0;
		u64 ee_cycle = 0;
		u64 iop_cycle = 0;
		u32 frame = 0;
		u64 host_time_ns = 0;
	};

	Point CaptureNext(u64& ordinal);
} // namespace AVPE::LoadTimingPoint
