// Validated AVP:E guest-object reads. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE::GuestObjects
{
	bool ReadWord(u32 address, u32* value);
	bool IsPlausibleAddress(u32 address);
	bool IsPlausibleObject(u32 address);
	bool ResolveHandle(u32 handle, u32* object);
} // namespace AVPE::GuestObjects
