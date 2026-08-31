// AVP:E shell-shutdown BIOS/IOP capture boundary. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <chrono>
#include <string>

namespace AVPE::NativeShellShutdownBoundary
{
	// Arms an exact CShell::Quit to CShell::MainLoop-return observation. Returns
	// false unless the current VM is the supported surfaceless AVP:E target.
	bool Start();

	// The EE recompiler consults this before a control request can arm shutdown.
	bool ShouldInstrumentEePc(u32 pc);
	void ObserveEeExecution(u32 pc);

	// Waits for the requested guest shutdown to return from the shell loop. A
	// structured incomplete result distinguishes an absent QuitGame activation
	// from an unrelated host/VM shutdown.
	std::string CaptureJson(std::chrono::milliseconds timeout);
} // namespace AVPE::NativeShellShutdownBoundary
