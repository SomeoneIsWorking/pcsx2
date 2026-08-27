// AVP:E native asset save-state diagnostics. Fork-local; not for upstream PCSX2.

#pragma once

#include <string>

namespace AVPE::NativeAssetStateSnapshot
{
	// The caller must capture from the CPU thread so descriptor cursors and
	// guest-owned CDVD mappings describe one save/load boundary.
	std::string CaptureJsonOnCPUThread();
} // namespace AVPE::NativeAssetStateSnapshot
