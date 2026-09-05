// Validated AVP:E guest-object reads. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE::GuestObjects
{
	inline constexpr u32 AttractExitVtable = 0x00343AF0;

	bool ReadBytes(u32 address, void* destination, u32 size);
	bool ReadWord(u32 address, u32* value);
	bool IsPlausibleAddress(u32 address);
	bool IsPlausibleObject(u32 address);
	bool ResolveHandle(u32 handle, u32* object);
	bool ResolveMemberFunction(u32 object, u32 member_address, u32* function);
} // namespace AVPE::GuestObjects
