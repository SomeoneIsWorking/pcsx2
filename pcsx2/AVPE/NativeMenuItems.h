// AVP:E menu-item discovery. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/GuestObjects.h"
#include "AVPE/NativeMenuInput.h"

#include <functional>

namespace AVPE::NativeMenuItems
{
	using Status = NativeMenuInput::Status;

	struct CallbackTarget
	{
		u32 object = 0;
		u32 callback = 0;
		u32 function = 0;
	};

	// Read-only guest boundary; synthetic fixtures exercise the same discovery.
	struct Access
	{
		std::function<bool(u32, u32*)> word = GuestObjects::ReadWord;
		std::function<bool(u32)> is_object = GuestObjects::IsPlausibleObject;
		std::function<bool(u32, u32*)> handle = GuestObjects::ResolveHandle;
		std::function<bool(u32, u32, u32*)> member = GuestObjects::ResolveMemberFunction;
	};

	// Prefer the existing ActivateFocused hotkey. Otherwise admit only the
	// current focused descendant's registered GMenuItem::HotKeyActivate.
	Status FindActivationCallback(u32 entries, u32 count, u32 menu, u32 focused,
		CallbackTarget* target, const char** error, const Access& read = {});
	Status FindMissionGoalsExitItem(u32 menu, u32* exit_item, const char** error,
		const Access& read = {});
} // namespace AVPE::NativeMenuItems
