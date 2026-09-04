// AVPE — RE-driven control channel for Aliens Versus Predator: Extinction.
// Fork-local module (not upstream PCSX2). Loopback HTTP server via lucent;
// handlers expose EE memory access and savestate control so external tooling
// can drive the game. See docs/re/input-path.md in the superproject.

#pragma once

namespace AVPE
{
	struct ButtonInjectionState
	{
		u32 inputs_mask;
		u32 wire_mask;
	};

	// Selected only by the recognized -avpe-control-test application mode.
	void SetSurfacelessControlTest(bool enabled);
	bool IsSurfacelessControlTest();
	void NoteControlTestRenderWindow(bool surfaceless);

	// Idempotent. Starts the loopback HTTP control server (port: env
	// AVPE_HTTP_PORT, default 28447). Returns false only on bind failure.
	bool Start();
	void Shutdown();

	// Button injection in PadDualshock2::Inputs bit space (bit i = enum value i),
	// consumed by PadDualshock2::GetButtons. Port 0 only. Mask expires after ms;
	// the control route admits durations from 1 through 60,000 milliseconds.
	void PressButtons(u32 mask, u32 ms);
	ButtonInjectionState ActiveButtonInjection();

	// Diagnostics fed by the SIO layer: cumulative pad-transfer count and the
	// response bytes of the most recent transfer ("" if none yet).
	void NotePadTransfer(int port, const char* fifo_bytes, u32 fifo_len);
	u32 TransferCount();
	std::string LastFifo();
} // namespace AVPE
