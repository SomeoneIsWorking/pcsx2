// Validated AVP:E guest-object reads. Fork-local; not for upstream PCSX2.

#include "AVPE/GuestObjects.h"

#include "vtlb.h"

#include <array>

namespace AVPE::GuestObjects
{
	static constexpr u32 TARGET_STATIC_BEGIN = 0x00100000;
	static constexpr u32 TARGET_STATIC_END = 0x00400000;
	static constexpr u32 HANDLE_ARRAY = 0x00371020;

	bool ReadWord(const u32 address, u32* value)
	{
		std::array<u8, 4> bytes{};
		if (!vtlb_memSafeReadBytes(address, bytes.data(), bytes.size()))
			return false;
		*value = static_cast<u32>(bytes[0]) |
		         (static_cast<u32>(bytes[1]) << 8) |
		         (static_cast<u32>(bytes[2]) << 16) |
		         (static_cast<u32>(bytes[3]) << 24);
		return true;
	}

	bool IsPlausibleAddress(const u32 address)
	{
		return address >= TARGET_STATIC_BEGIN && address < Ps2MemSize::ExposedRam && (address & 3) == 0;
	}

	bool IsPlausibleObject(const u32 address)
	{
		u32 vtable = 0;
		return IsPlausibleAddress(address) && ReadWord(address, &vtable) &&
		       vtable >= TARGET_STATIC_BEGIN && vtable < TARGET_STATIC_END && (vtable & 3) == 0;
	}

	bool ResolveHandle(const u32 handle, u32* object)
	{
		if (handle == 0)
		{
			*object = 0;
			return true;
		}
		const u32 index = handle >> 16;
		return ReadWord(HANDLE_ARRAY + index * sizeof(u32), object) &&
		       IsPlausibleObject(*object);
	}
} // namespace AVPE::GuestObjects
