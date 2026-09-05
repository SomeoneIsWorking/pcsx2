// AVP:E attract cancellation admission. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAttractInput.h"

namespace AVPE::NativeAttractInput
{
	static constexpr u32 BUTTON_INPUT = 0x00206A60;
	static constexpr u32 ANALOG_INPUT = 0x002069E0;

	Result FindCancellation(const u32 entries, const u32 count, const NativeInputCallbacks::Access& read)
	{
		if (count > NativeInputCallbacks::MaxCount)
			return {.status = Status::Invalid, .error = "attract callback registry exceeds its bound"};
		Result result;
		u32 attract_owner = 0;
		for (u32 index = 0; index < count; ++index)
		{
			const u32 callback = entries + index * NativeInputCallbacks::Stride;
			u32 function = 0;
			if (!read.word(callback + NativeInputCallbacks::DirectFunctionOffset, &function))
				return {.status = Status::Invalid, .error = "attract callback registry member is unreadable"};
			// Unrelated expired registry owners are valid background entries.
			if (function != BUTTON_INPUT && function != ANALOG_INPUT)
				continue;
			u32 handle = 0;
			u32 owner = 0;
			u32 vtable = 0;
			u32 resolved = 0;
			if (!read.word(callback + NativeInputCallbacks::OwnerOffset, &handle) ||
				!read.handle(handle, &owner) || !read.word(owner, &vtable) ||
				vtable != GuestObjects::AttractExitVtable ||
				!read.member(owner, callback + NativeInputCallbacks::MemberOffset, &resolved) ||
				resolved != function)
			{
				return {.status = Status::Invalid, .error = "attract callback owner or member is invalid"};
			}
			if (attract_owner != 0 && attract_owner != owner)
				return {.status = Status::Ambiguous, .error = "more than one attract input owner is registered"};
			attract_owner = owner;
			if (function == BUTTON_INPUT && result.target.object == 0)
			{
				result.status = Status::Available;
				result.target = {.object = owner, .callback = callback, .function = function};
			}
		}
		if (attract_owner != 0 && result.status != Status::Available)
			return {.status = Status::Invalid, .error = "attract owner has no registered button cancellation callback"};
		return result;
	}
} // namespace AVPE::NativeAttractInput
