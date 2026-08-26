// AVP:E native menu-action bridge. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/EECallShuttle.h"

namespace AVPE::NativeMenuInput
{
	enum class Action : u8
	{
		Up,
		Down,
		Left,
		Right,
		Activate,
		Cancel,
	};

	enum class Status : u8
	{
		Success,
		MenuUnavailable,
		AmbiguousMenu,
		GuestMemoryError,
		ShuttleFailure,
	};

	struct FocusState
	{
		u32 handle = 0;
		u32 object = 0;
	};

	struct Result
	{
		Status status = Status::ShuttleFailure;
		EECallShuttle::Status shuttle_status = EECallShuttle::Status::Interrupted;
		Action action = Action::Up;
		u32 menu = 0;
		u32 handler = 0;
		u32 callback_count = 0;
		FocusState before;
		FocusState after;
		u64 elapsed_cycles = 0;
		u64 deferred_call_id = 0;
		bool deferred = false;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	Result Inspect();
	Result Apply(Action action);
} // namespace AVPE::NativeMenuInput
