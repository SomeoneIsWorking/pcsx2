// AVP:E IOP execution-hook composition. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE::NativeIopExecutionHooks
{
	// Observe the first caller instruction after an oracle import has returned.
	void ObserveIopExecution(u32 pc);
} // namespace AVPE::NativeIopExecutionHooks
