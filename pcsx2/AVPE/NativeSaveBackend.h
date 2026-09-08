// AVP:E native profile persistence seam. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE::NativeProfileContract
{
	struct Snapshot;
}

namespace AVPE::NativeSaveBackend
{

	// Persists only the validated CProfile payload at the title's completed save
	// boundary. The numbered game records remain in the existing card path until
	// their native writer and loader are grounded.
	void ObserveProfileEntry(const NativeProfileContract::Snapshot& snapshot);
	void ObserveProfileReturn(s32 result);

	void ObserveProfileLoadEntry(const NativeProfileContract::Snapshot& snapshot);

	// Restores a previously persisted profile payload into the live CProfile data
	// buffer before CProfile::LoadGame reads the selected record.
} // namespace AVPE::NativeSaveBackend
