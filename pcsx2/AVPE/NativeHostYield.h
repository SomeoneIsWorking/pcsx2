// AVP:E title-owned host-work yield boundaries. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE::NativeHostYield
{
	void Request();
	void CompleteRequest();
	bool ShouldInstrumentEePc(u32 pc);
	void ObserveEeExecution(u32 pc);
} // namespace AVPE::NativeHostYield
