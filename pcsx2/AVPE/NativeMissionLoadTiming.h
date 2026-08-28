// AVP:E mission-load timing evidence. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <string>

namespace AVPE::NativeMissionLoadTiming
{
	// The recompiler uses this to make each observed PC a block entry. The
	// interpreter may use it as a cheap address guard before the runtime call.
	bool ShouldInstrumentEePc(u32 pc);
	void ObserveEeExecution(u32 pc);
	std::string SnapshotJson();
	void Reset();
} // namespace AVPE::NativeMissionLoadTiming
