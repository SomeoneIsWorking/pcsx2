// AVP:E callback discovery contracts. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/GuestObjects.h"

#include <functional>

namespace AVPE::NativeInputCallbacks
{
	inline constexpr u32 MaxCount = 256;
	inline constexpr u32 Stride = 0x18;
	inline constexpr u32 OwnerOffset = 0x08;
	inline constexpr u32 MemberOffset = 0x0C;
	inline constexpr u32 DirectFunctionOffset = 0x14;
	inline constexpr u32 InputDeviceSingleton = 0x00366E68;
	// GMenuItem's pad callbacks: the hotkey activates an item, the focus key focuses it.
	inline constexpr u32 MenuItemHotKeyActivate = 0x00120F40;
	inline constexpr u32 MenuItemFocusKeyActivate = 0x00120F90;
	// GInputDevice's callback ZArray: entries, count and capacity words.
	inline constexpr u32 RegistryOffset = 0x48;

	struct Registry
	{
		u32 entries = 0;
		u32 count = 0;
		u32 capacity = 0;
	};

	struct Target
	{
		u32 object = 0;
		u32 callback = 0;
		u32 function = 0;
	};

	// Synthetic fixtures replace only guest reads, never discovery policy.
	struct Access
	{
		std::function<bool(u32, u32*)> word = GuestObjects::ReadWord;
		std::function<bool(u32)> is_object = GuestObjects::IsPlausibleObject;
		std::function<bool(u32)> is_address = GuestObjects::IsPlausibleAddress;
		std::function<bool(u32, u32*)> handle = GuestObjects::ResolveHandle;
		std::function<bool(u32, u32, u32*)> member = GuestObjects::ResolveMemberFunction;
	};

	// The device's callback array; false when it is unreadable or its bounds are implausible.
	bool ReadRegistry(u32 input_device, const Access& read, Registry* registry);

	// The first registered callback an owner of this class resolves to function; false when
	// there is none, owners of it are several, or the registry is unreadable.
	bool FindRegistered(u32 entries, u32 count, u32 vtable, u32 function, Target* target, const Access& read);
} // namespace AVPE::NativeInputCallbacks
