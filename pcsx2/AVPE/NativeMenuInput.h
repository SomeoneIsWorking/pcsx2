// AVP:E native menu-action bridge. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/EECallShuttle.h"

#include <string>

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
		u32 vtable = 0;
		u32 text_address = 0;
	};

	struct Result
	{
		Status status = Status::ShuttleFailure;
		EECallShuttle::Status shuttle_status = EECallShuttle::Status::Interrupted;
		Action action = Action::Up;
		Source source = Source::None;
		u32 menu = 0;
		u32 menu_vtable = 0;
		u32 conflicting_menu = 0;
		u32 conflicting_menu_vtable = 0;
		u32 handler = 0;
		u32 action_target = 0;
		u32 focused_item_action = 0;
		bool focused_item_action_valid = false;
		u32 callback_count = 0;
		FocusState before;
		FocusState after;
		u64 elapsed_cycles = 0;
		u32 stopped_pc = 0;
		u32 last_avpe_text_pc = 0;
		u64 dispatch_action_id = 0;
		u64 deferred_call_id = 0;
		bool stack_restored = true;
		bool deferred = false;
		u64 readiness_action_id = 0;
		bool awaiting_readiness = false;
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
		u32 callback = 0;
		u32 handler = 0;
		u32 callback_count = 0;
		FocusState before;
		FocusState after;
		u32 focused_item_action = 0;
		bool focused_item_action_valid = false;
		float screen_x = 0.0f;
		float screen_y = 0.0f;
		float observed_x = 0.0f;
		float observed_y = 0.0f;
		float menu_x = 0.0f;
		float menu_y = 0.0f;
		u32 staging_address = 0;
		u32 return_pc = 0;
		u32 stopped_pc = 0;
		u32 last_avpe_text_pc = 0;
		u64 elapsed_cycles = 0;
		u64 dispatch_pointer_id = 0;
		u64 deferred_call_id = 0;
		bool stack_restored = false;
		bool deferred = false;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	Result Inspect();
	Result Apply(Action action);
	// Arms one exact-vtable/focus physical-pad action for the next matching
	// normal input dispatch. It never retries after admission or a failed validation.
	Result ApplyWhenReady(Action action, u32 menu_vtable, u32 focused_item_action);
	bool ShouldObserveEePc(u32 pc);
	void ObserveInputProcess();
	std::string PendingActionJson();
	void Reset();
	PointerResult InspectPointer();
	PointerResult MovePointer(float normalized_x, float normalized_y);
	PointerResult MovePointerThroughDispatch(float normalized_x, float normalized_y);
	PointerResult ActivatePointer();
} // namespace AVPE::NativeMenuInput
