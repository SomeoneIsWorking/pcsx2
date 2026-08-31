// AVP:E native input callback-dispatch evidence. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <string>

namespace AVPE::NativeInputDispatch
{
	// The recompiler uses this to make the member-callback dispatch an exact
	// block entry. The observer performs the live title/control-test gate.
	bool ShouldInstrumentEePc(u32 pc);
	void ObserveEeExecution(u32 pc);
	std::string SnapshotJson();
	void Reset();
} // namespace AVPE::NativeInputDispatch
