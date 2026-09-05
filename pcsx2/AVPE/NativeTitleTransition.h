// AVP:E title-to-profile lifecycle observation. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <string>

namespace AVPE::NativeTitleTransition
{
	// Arms a passive observation on the supported surfaceless AVP:E target.
	// It never injects input, calls guest code, or writes guest memory.
	bool Start();
	void Reset();

	// The EE recompiler consults this for the two statically grounded entries.
	bool ShouldInstrumentEePc(u32 pc);
	void ObserveEeExecution(u32 pc);

	// Returns the bounded state, including the first observed ordered entries.
	std::string SnapshotJson();
} // namespace AVPE::NativeTitleTransition
