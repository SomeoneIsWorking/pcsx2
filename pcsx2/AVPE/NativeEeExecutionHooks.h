// AVP:E EE execution-hook composition. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE::NativeEeExecutionHooks
{
	bool ShouldInstrumentEePc(u32 pc);
	void ObserveEeExecution(u32 pc);
} // namespace AVPE::NativeEeExecutionHooks
