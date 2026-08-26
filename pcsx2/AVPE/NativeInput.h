// AVP:E native keyboard/mouse bridge. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/EECallShuttle.h"

namespace AVPE::NativeInput
{
	enum class Status : u8
	{
		Success,
		InvalidCoordinates,
		ResolutionUnavailable,
		PointerUnavailable,
		SelectorModeRejected,
		GuestMemoryError,
		ShuttleFailure,
	};

	struct Result
	{
		Status status = Status::ShuttleFailure;
		EECallShuttle::Status shuttle_status = EECallShuttle::Status::Interrupted;
		float screen_x = 0.0f;
		float screen_y = 0.0f;
		float observed_x = 0.0f;
		float observed_y = 0.0f;
		u32 pointer = 0;
		u32 staging_address = 0;
		u64 elapsed_cycles = 0;
		bool stack_restored = false;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	// Coordinates are normalized to the current game resolution. The native
	// bridge reasserts absolute selector mode and invokes the game's own pointer
	// update function; it does not emulate a pad or write pointer fields directly.
	Result MoveAbsolute(float normalized_x, float normalized_y);
} // namespace AVPE::NativeInput
