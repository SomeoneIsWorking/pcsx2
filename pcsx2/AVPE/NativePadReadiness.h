// AVP:E guest pad polling readiness. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/GuestObjects.h"

#include <functional>

namespace AVPE::NativePadReadiness
{
	using ReadWord = std::function<bool(u32, u32*)>;

	// Called at GInputDevice::Process entry, on the CPU thread. This only reads
	// the title's poller; it never initializes the pad or changes guest input.
	bool IsReady(u32 input_device, const ReadWord& read = GuestObjects::ReadWord);
} // namespace AVPE::NativePadReadiness
