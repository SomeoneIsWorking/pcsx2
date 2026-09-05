// AVP:E attract cancellation admission. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/NativeInputCallbacks.h"

namespace AVPE::NativeAttractInput
{
	enum class Status : u8
	{
		Absent,
		Available,
		Invalid,
		Ambiguous,
	};

	struct Result
	{
		Status status = Status::Absent;
		NativeInputCallbacks::Target target;
		const char* error = "";
	};

	// Select the existing button callback, never the analogue callback or a
	// shell phase mutation. The guest owns the resulting level/owner teardown.
	Result FindCancellation(u32 entries, u32 count, const NativeInputCallbacks::Access& read = {});
} // namespace AVPE::NativeAttractInput
