// AVP:E callback discovery contracts. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeInputCallbacks.h"

namespace AVPE::NativeInputCallbacks
{
	bool FindRegistered(const u32 entries, const u32 count, const u32 vtable, const u32 function, Target* target,
		const Access& read)
	{
		*target = {};
		for (u32 index = 0; index < count; ++index)
		{
			const u32 callback = entries + index * Stride;
			u32 handle = 0;
			u32 owner = 0;
			u32 owner_vtable = 0;
			if (!read.word(callback + OwnerOffset, &handle))
			{
				return false;
			}
			// Owners of every class register here; only a resolvable owner of this class is wanted.
			if (handle == 0 || !read.handle(handle, &owner) || !read.word(owner, &owner_vtable) ||
				owner_vtable != vtable)
			{
				continue;
			}
			u32 resolved = 0;
			if (!read.member(owner, callback + MemberOffset, &resolved))
			{
				return false;
			}
			if (resolved != function)
			{
				continue;
			}
			if (target->object != 0)
			{
				return false;
			}
			*target = {.object = owner, .callback = callback, .function = resolved};
		}
		return target->object != 0;
	}
} // namespace AVPE::NativeInputCallbacks
