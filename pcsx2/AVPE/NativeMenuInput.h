// AVP:E native menu-action bridge. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/EECallShuttle.h"

namespace AVPE::NativeMenuInput
{
	enum class Source : u8
	{
		None,
		CallbackRegistry,
		MissionGoalsLoad,
	};

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
		InvalidCoordinates,
		MenuUnavailable,
		AmbiguousMenu,
		PointerUnavailable,
		AmbiguousPointer,
		FocusUnavailable,
		ResolutionUnavailable,
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
		Source source = Source::None;
		u32 menu = 0;
		u32 handler = 0;
		u32 action_target = 0;
		u32 callback_count = 0;
		FocusState before;
		FocusState after;
		u64 elapsed_cycles = 0;
		u32 stopped_pc = 0;
		u64 deferred_call_id = 0;
		bool stack_restored = true;
		bool deferred = false;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	Source IdentifyMenuSource(u32 callback_menu, u32 mission_goals_menu, u32 mission_goals_vtable);
	const char* SourceName(Source source);

	struct PointerResult
	{
		Status status = Status::ShuttleFailure;
		EECallShuttle::Status shuttle_status = EECallShuttle::Status::Interrupted;
		u32 pointer = 0;
		u32 handler = 0;
		u32 callback_count = 0;
		FocusState before;
		FocusState after;
		float screen_x = 0.0f;
		float screen_y = 0.0f;
		float observed_x = 0.0f;
		float observed_y = 0.0f;
		u32 staging_address = 0;
		u64 elapsed_cycles = 0;
		u64 deferred_call_id = 0;
		bool stack_restored = false;
		bool deferred = false;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	Result Inspect();
	Result Apply(Action action);
	PointerResult InspectPointer();
	PointerResult MovePointer(float normalized_x, float normalized_y);
	PointerResult ActivatePointer();
} // namespace AVPE::NativeMenuInput
