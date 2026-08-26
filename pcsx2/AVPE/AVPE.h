// AVPE — RE-driven control channel for Aliens Versus Predator: Extinction.
// Fork-local module (not upstream PCSX2). Loopback HTTP server via lucent;
// handlers expose EE memory access and savestate control so external tooling
// can drive the game. See docs/re/input-path.md in the superproject.

#pragma once

namespace AVPE
{
	// Idempotent. Starts the loopback HTTP control server (port: env
	// AVPE_HTTP_PORT, default 28447). Returns false only on bind failure.
	bool Start();
	void Shutdown();
} // namespace AVPE
