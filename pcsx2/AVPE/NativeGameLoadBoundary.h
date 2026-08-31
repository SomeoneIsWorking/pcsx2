// AVP:E game-load BIOS/IOP capture boundary. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <chrono>
#include <string>

namespace AVPE::NativeGameLoadBoundary
{
	// Arms the exact CProfile::LoadGame observation. Returns false unless the
	// current VM is the supported surfaceless AVP:E control-test target.
	bool Start();

	// The EE recompiler consults this before a control request can arm a load.
	bool ShouldInstrumentEePc(u32 pc);
	void ObserveEeExecution(u32 pc);

	// Waits for the armed normal game-load call to return. A structured
	// incomplete result distinguishes a missing entry/return from a success or
	// game-reported failure.
	std::string CaptureJson(std::chrono::milliseconds timeout);
} // namespace AVPE::NativeGameLoadBoundary
