// AVP:E native input callback-dispatch evidence. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <string>

namespace AVPE::NativeInputDispatch
{
	enum class Status : u8
	{
		Success,
		Busy,
		InvalidPointerCallback,
		InvalidMenuCallback,
	};

	struct PointerMotionRequest
	{
		u32 pointer = 0;
		u32 callback = 0;
		float x = 0.0f;
		float y = 0.0f;
	};

	struct MenuActionRequest
	{
		u32 target = 0;
		u32 callback = 0;
		u32 function = 0;
	};

	struct Result
	{
		Status status = Status::InvalidPointerCallback;
		u64 id = 0;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	// Queue exactly one derived-pointer relative-motion callback for the
	// ordinary GInputDevice dispatch. This must run on the EE CPU thread.
	Result QueuePointerMotion(const PointerMotionRequest& request);

	// Queue one already-registered menu, menu-item, or attract input callback for the next
	// ordinary GInputDevice dispatch. NativeMenuInput validates its action
	// meaning and ownership; this owner only preserves the guest dispatch ABI.
	Result QueueMenuAction(const MenuActionRequest& request);

	// The recompiler uses this to make the member-callback dispatch an exact
	// block entry. The observer performs the live title/control-test gate.
	bool ShouldInstrumentEePc(u32 pc);
	void ObserveEeExecution(u32 pc);
	std::string SnapshotJson();
	void Reset();
} // namespace AVPE::NativeInputDispatch
