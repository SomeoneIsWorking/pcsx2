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
		AttractCancellation,
		MovieCancellation,
		LoadErrorConfirmation,
		IntroSkip,
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
	inline constexpr size_t ActionCount = static_cast<size_t>(Action::Cancel) + 1;

	// GLevelLoadErrorMenu, CShell::MainLoop's modal after a failed game load: its
	// FrontEndSelect callback, Input_Exit, ends the modal.
	inline constexpr u32 LoadErrorMenuVtable = 0x00342B50;
	inline constexpr u32 LoadErrorExitFunction = 0x00209E50;
	// GSkipLevelIntro, the mission intro's skip button: its registered hotkey,
	// GMenuItem::HotKeyActivate, activates it and its Process stops the intro.
	inline constexpr u32 IntroSkipButtonVtable = 0x00349B80;

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
		// The guest's own name hash for this item. Sibling items can share an
		// action value, so callers that must tell items apart read this.
		u32 name = 0;
		bool name_valid = false;
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
		u64 movie_action_id = 0;
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
	// Activates one live menu item through its own registered hotkey callback, focused or
	// not, whether or not a navigation menu is active.
	Result ActivateItem(u32 item);
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
