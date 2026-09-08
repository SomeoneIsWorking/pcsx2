// AVP:E live CProfile contract. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <optional>
#include <vector>

namespace AVPE::NativeProfileContract
{

	struct Snapshot
	{
		u32 object = 0;
		u32 data = 0;
		u32 size = 0;
		u32 revision = 0;
		u32 slot_count = 0;
		std::vector<u8> payload;
	};

	std::optional<Snapshot> CaptureCurrent();
	std::optional<Snapshot> CaptureObject(u32 object);
	bool RestorePayload(const Snapshot& snapshot);

} // namespace AVPE::NativeProfileContract
