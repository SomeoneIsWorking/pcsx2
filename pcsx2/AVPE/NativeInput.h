// AVP:E native keyboard/mouse bridge. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/EECallShuttle.h"

namespace AVPE::NativeInput
{
	enum class Status : u8
	{
		Success,
		InvalidCoordinates,
		InvalidButtonEdge,
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

	enum class MouseButton : u8
	{
		Primary,
		Secondary,
	};

	enum class ButtonEdge : u8
	{
		Press,
		Release,
	};

	// How a primary release changes the selection: the PC's plain, Shift and Ctrl clicks.
	enum class SelectionMode : u8
	{
		Replace,
		// GfsPointer::Select adds the box and toggles a single clicked unit.
		Toggle,
		// Every on-screen unit of the clicked unit's type, as on a double click.
		SameType,
	};

	struct SelectionState
	{
		u32 count = 0;
		u32 selected_mark = 0;
		u32 selected_object = 0;
		u32 command_id = 0;
	};

	struct ButtonResult
	{
		Status status = Status::ShuttleFailure;
		EECallShuttle::Status shuttle_status = EECallShuttle::Status::Interrupted;
		MouseButton button = MouseButton::Primary;
		ButtonEdge edge = ButtonEdge::Press;
		u32 pointer = 0;
		// The selection when the edge was queued; its calls run at the next safe boundary.
		SelectionState before;
		// The deferred ticket holding the edge's calls; 0 when the edge does nothing.
		u64 deferred_call_id = 0;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	// Coordinates are normalized to the current game resolution. The native
	// bridge reasserts absolute selector mode and invokes the game's own pointer
	// update function; it does not emulate a pad or write pointer fields directly.
	Result MoveAbsolute(float normalized_x, float normalized_y);

	// Queues the edge's NativeMouseButtons calls in order on the deferred shuttle and rejects impossible duplicate edges. The mode
	// applies to a primary release only; other edges take Replace.
	ButtonResult ApplyButtonEdge(MouseButton button, ButtonEdge edge, SelectionMode mode);
	void ResetAfterStateLoad();
} // namespace AVPE::NativeInput
