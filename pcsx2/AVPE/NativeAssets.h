// AVP:E native asset-I/O boundary. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Types.h"

#include <string>
#include <string_view>
#include <vector>

namespace AVPE::NativeAssets
{
	struct OpenObservation
	{
		std::string path;
		u32 flags = 0;
		u32 count = 0;
	};

	struct ObservationSnapshot
	{
		bool enabled = false;
		bool target_recognized = false;
		u64 total_open_calls = 0;
		u64 dropped_unique_paths = 0;
		std::vector<OpenObservation> paths;
	};

	// Called at the IOP ioman/iomanX open boundary. This is observation-only:
	// it never claims the request or changes the original IOP path.
	void ObserveIomanOpen(std::string_view path, u32 flags);
	ObservationSnapshot GetObservationSnapshot();
	void ResetObservation();
} // namespace AVPE::NativeAssets
