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
		std::function<bool(u32, u32*)> handle = GuestObjects::ResolveHandle;
		std::function<bool(u32, u32, u32*)> member = GuestObjects::ResolveMemberFunction;
	};
} // namespace AVPE::NativeInputCallbacks
