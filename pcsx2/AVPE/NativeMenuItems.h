// AVP:E menu-item discovery. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/NativeInputCallbacks.h"
#include "AVPE/NativeMenuInput.h"

namespace AVPE::NativeMenuItems
{
	using Status = NativeMenuInput::Status;

	// Prefer the existing ActivateFocused hotkey. Otherwise admit only the
	// current focused descendant's registered GMenuItem::HotKeyActivate.
	// An attract owner keeps activation in its separate cancellation path.
	Status FindActivationCallback(u32 entries, u32 count, u32 menu, u32 focused,
		NativeInputCallbacks::Target* target, const char** error,
		const NativeInputCallbacks::Access& read = {});
	Status FindMissionGoalsExitItem(u32 menu, u32* exit_item, const char** error,
		const NativeInputCallbacks::Access& read = {});
} // namespace AVPE::NativeMenuItems
