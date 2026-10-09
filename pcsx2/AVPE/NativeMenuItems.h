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
	// The item's registered GMenuItem::HotKeyActivate within its parent, focused or not.
	Status FindItemCallback(u32 entries, u32 count, u32 item,
		NativeInputCallbacks::Target* target, const char** error,
		const NativeInputCallbacks::Access& read = {});
	// A focused GSliderControl owns horizontal adjustment; ordinary menu
	// navigation remains with the menu. Dispatch uses its registered guest member.
	Status FindAdjustmentCallback(u32 entries, u32 count, u32 menu, u32 focused,
		NativeMenuInput::Action action, NativeInputCallbacks::Target* target,
		const char** error, const NativeInputCallbacks::Access& read = {});
	// The registered HotKeyActivate of the menu's item bound to this pad event (GMenuItem+0x118).
	Status FindHotkeyItem(u32 entries, u32 count, u32 menu, u32 hotkey,
		NativeInputCallbacks::Target* target, const char** error,
		const NativeInputCallbacks::Access& read = {});
	// Cancellation is the registered HotKeyActivate of the menu's Back item.
	// FocusUnavailable means this menu keeps its existing virtual cancellation.
	Status FindCancellationCallback(u32 entries, u32 count, u32 menu,
		NativeInputCallbacks::Target* target, const char** error,
		const NativeInputCallbacks::Access& read = {});
	// The registered HotKeyActivate of the menu's item whose PC letter is letter, the
	// letters taken by label in registry order as NativePromptKeys assigns them.
	Status FindCommandItem(u32 entries, u32 count, u32 menu, char letter,
		NativeInputCallbacks::Target* target, const char** error,
		const NativeInputCallbacks::Access& read = {});
	Status FindMissionGoalsExitItem(u32 menu, u32* exit_item, const char** error,
		const NativeInputCallbacks::Access& read = {});
	// The guest's own 32-bit name hash carried by every menu item. Sibling items
	// in one menu can report an identical action value, so this is the
	// discriminating per-item identity for discovery and observation.
	bool ReadItemName(u32 item, u32* name, const NativeInputCallbacks::Access& read = {});
} // namespace AVPE::NativeMenuItems
