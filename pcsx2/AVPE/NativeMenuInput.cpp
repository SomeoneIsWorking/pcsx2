// AVP:E native menu-action bridge. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMenuInput.h"

#include "AVPE/GuestObjects.h"

#include <array>

namespace AVPE::NativeMenuInput
{
	static constexpr u32 INPUT_DEVICE_SINGLETON = 0x00366E68;
	static constexpr u32 CALLBACK_ARRAY_OFFSET = 0x48;
	static constexpr u32 FOCUSED_ITEM_HANDLE_OFFSET = 0x26C;
	static constexpr u32 CALLBACK_STRIDE = 0x18;
	static constexpr u32 CALLBACK_OWNER_OFFSET = 0x08;
	static constexpr u32 CALLBACK_FUNCTION_OFFSET = 0x14;
	static constexpr u32 MENU_INPUT = 0x00125330;
	static constexpr u32 MENU_CANCEL_VTABLE_OFFSET = 0xFC;
	static constexpr u32 MAX_CALLBACK_COUNT = 256;
	static constexpr std::array<u32, 6> MENU_CALLBACKS = {
		0x00124BD0,
		0x00124BE0,
		0x00124BF0,
		0x00124C00,
		0x00124C10,
		0x00125230,
	};

	struct ActiveMenu
	{
		u32 object = 0;
		u32 callback_count = 0;
	};

	static bool IsMenuCallback(const u32 function)
	{
		for (const u32 candidate : MENU_CALLBACKS)
		{
			if (candidate == function)
				return true;
		}
		return false;
	}

	static Status FindActiveMenu(ActiveMenu* active, const char** error)
	{
		*active = {};
		u32 input_device = 0;
		u32 callbacks = 0;
		if (!GuestObjects::ReadWord(INPUT_DEVICE_SINGLETON, &input_device) ||
			!GuestObjects::IsPlausibleObject(input_device) ||
			!GuestObjects::ReadWord(input_device + CALLBACK_ARRAY_OFFSET, &callbacks) ||
			!GuestObjects::ReadWord(input_device + CALLBACK_ARRAY_OFFSET + sizeof(u32),
				&active->callback_count) ||
			active->callback_count > MAX_CALLBACK_COUNT)
		{
			*error = "game input callback registry is invalid or unreadable";
			return Status::GuestMemoryError;
		}
		if (active->callback_count == 0 || !GuestObjects::IsPlausibleAddress(callbacks))
		{
			*error = "no active game menu callbacks are registered";
			return Status::MenuUnavailable;
		}

		for (u32 i = 0; i < active->callback_count; ++i)
		{
			const u32 callback = callbacks + i * CALLBACK_STRIDE;
			u32 function = 0;
			if (!GuestObjects::ReadWord(callback + CALLBACK_FUNCTION_OFFSET, &function))
			{
				*error = "game input callback entry is unreadable";
				return Status::GuestMemoryError;
			}
			if (!IsMenuCallback(function))
				continue;

			u32 owner_handle = 0;
			u32 owner = 0;
			if (!GuestObjects::ReadWord(callback + CALLBACK_OWNER_OFFSET, &owner_handle) ||
				!GuestObjects::ResolveHandle(owner_handle, &owner))
			{
				*error = "menu callback owner handle is invalid";
				return Status::GuestMemoryError;
			}
			if (active->object != 0 && active->object != owner)
			{
				*error = "more than one game menu owns active navigation callbacks";
				return Status::AmbiguousMenu;
			}
			active->object = owner;
		}

		if (active->object == 0)
		{
			*error = "no active game menu owns navigation callbacks";
			return Status::MenuUnavailable;
		}
		return Status::Success;
	}

	static bool ReadFocus(const u32 menu, FocusState* focus)
	{
		*focus = {};
		return GuestObjects::ReadWord(menu + FOCUSED_ITEM_HANDLE_OFFSET, &focus->handle) &&
		       GuestObjects::ResolveHandle(focus->handle, &focus->object);
	}

	static bool ReadCancelHandler(const u32 menu, u32* handler)
	{
		u32 vtable = 0;
		return GuestObjects::ReadWord(menu, &vtable) &&
		       GuestObjects::IsPlausibleAddress(vtable) &&
		       GuestObjects::ReadWord(vtable + MENU_CANCEL_VTABLE_OFFSET, handler) &&
		       GuestObjects::IsPlausibleAddress(*handler);
	}

	static void InspectOnCPUThread(Result* result)
	{
		ActiveMenu active;
		result->status = FindActiveMenu(&active, &result->error);
		result->callback_count = active.callback_count;
		result->menu = active.object;
		if (result->status != Status::Success)
			return;
		if (!ReadFocus(result->menu, &result->before))
		{
			result->status = Status::GuestMemoryError;
			result->error = "focused game menu item is invalid or unreadable";
		}
	}

	Result Inspect()
	{
		Result result;
		EECallShuttle::RunTransaction(
			[&result](EECallShuttle::Transaction&) { InspectOnCPUThread(&result); });
		return result;
	}

	Result Apply(const Action action)
	{
		Result result{.action = action};
		EECallShuttle::RunTransaction([&result, action](EECallShuttle::Transaction& transaction) {
			InspectOnCPUThread(&result);
			if (result.status != Status::Success)
				return;

			result.handler = MENU_INPUT;
			if (action == Action::Cancel && !ReadCancelHandler(result.menu, &result.handler))
			{
				result.status = Status::GuestMemoryError;
				result.error = "active menu cancel handler is invalid or unreadable";
				return;
			}
			EECallShuttle::Request request{.function = result.handler};
			request.arguments = {
				result.menu,
				action == Action::Cancel ? 0 : static_cast<u32>(action),
				0,
				0,
			};
			if (action == Action::Activate || action == Action::Cancel)
			{
				const EECallShuttle::DeferredTicket ticket = transaction.QueueDeferred(request);
				result.shuttle_status = ticket.status;
				result.deferred_call_id = ticket.id;
				result.deferred = ticket.Accepted();
				if (!ticket.Accepted())
				{
					result.status = ticket.status == EECallShuttle::Status::GuestMemoryError ?
					                    Status::GuestMemoryError :
					                    Status::ShuttleFailure;
					result.error = ticket.error;
					return;
				}
				result.status = Status::Success;
				return;
			}

			const EECallShuttle::Result call = transaction.Call(request);
			result.shuttle_status = call.status;
			result.elapsed_cycles = call.elapsed_cycles;
			if (!call.Succeeded())
			{
				result.status = call.status == EECallShuttle::Status::GuestMemoryError ?
				                    Status::GuestMemoryError :
				                    Status::ShuttleFailure;
				result.error = call.error;
				return;
			}
			if (!ReadFocus(result.menu, &result.after))
				result.after = {};
			result.status = Status::Success;
		});
		return result;
	}
} // namespace AVPE::NativeMenuInput
