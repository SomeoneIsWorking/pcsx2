// AVP:E memory-card readiness diagnostics. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMemoryCardState.h"

#include "Host.h"
#include "SIO/Sio.h"

namespace AVPE::NativeMemoryCardState
{
	Snapshot Capture()
	{
		Snapshot snapshot;
		Host::RunOnCPUThread(
			[&snapshot]() {
				_mcd& card = mcds[0][0];
				snapshot.present = card.IsPresent();
				snapshot.busy = MemcardBusy::IsBusy();
				snapshot.auto_eject_ticks = static_cast<u32>(card.autoEjectTicks);
			},
			true);
		return snapshot;
	}
} // namespace AVPE::NativeMemoryCardState
