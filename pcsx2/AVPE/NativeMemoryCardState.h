// AVP:E memory-card readiness diagnostics. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE::NativeMemoryCardState
{
	struct Snapshot
	{
		bool present = false;
		bool busy = false;
		u32 auto_eject_ticks = 0;

		bool Ready() const { return present && !busy && auto_eject_ticks == 0; }
	};

	// Safe from a control-server thread. The state is sampled synchronously on
	// the CPU thread that owns SIO's memory-card lifecycle.
	Snapshot Capture();
} // namespace AVPE::NativeMemoryCardState
