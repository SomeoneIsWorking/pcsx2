// AVP:E native menu-action bridge. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMenuInput.h"

#include "AVPE/GuestObjects.h"
#include "AVPE/NativePointerMotion.h"

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
	static constexpr u32 MENU_POINTER_FOCUS_HANDLE_OFFSET = 0x1AC;
	static constexpr u32 MENU_POINTER_CHECK = 0x0012E490;
	static constexpr u32 MENU_POINTER_ACTION = 0x0012EBB0;
	static constexpr u32 GET_MENU_ITEM = 0x0012E8C0;
	static constexpr u32 UPDATE_POINTER_POSITION_ABSOLUTE = 0x0012EAB0;
	static constexpr u32 GET_MENU_ITEM_VTABLE_OFFSET = 0xD4;
	static constexpr u32 UPDATE_POINTER_ABSOLUTE_VTABLE_OFFSET = 0xDC;
	static constexpr u32 POINTER_ACTION_VTABLE_OFFSET = 0xE0;
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

	struct CallbackRegistry
	{
		u32 entries = 0;
		u32 count = 0;
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

	static Status ReadCallbackRegistry(CallbackRegistry* registry, const char** error)
	{
		*registry = {};
		u32 input_device = 0;
		if (!GuestObjects::ReadWord(INPUT_DEVICE_SINGLETON, &input_device) ||
			!GuestObjects::IsPlausibleObject(input_device) ||
			!GuestObjects::ReadWord(input_device + CALLBACK_ARRAY_OFFSET, &registry->entries) ||
			!GuestObjects::ReadWord(input_device + CALLBACK_ARRAY_OFFSET + sizeof(u32),
				&registry->count) ||
			registry->count > MAX_CALLBACK_COUNT)
		{
			*error = "game input callback registry is invalid or unreadable";
			return Status::GuestMemoryError;
		}
		if (registry->count == 0 || !GuestObjects::IsPlausibleAddress(registry->entries))
		{
			*error = "no active game input callbacks are registered";
			return Status::MenuUnavailable;
		}
		return Status::Success;
	}

	static Status FindActiveMenu(ActiveMenu* active, const char** error)
	{
		*active = {};
		CallbackRegistry registry;
		const Status registry_status = ReadCallbackRegistry(&registry, error);
		active->callback_count = registry.count;
		if (registry_status != Status::Success)
			return registry_status;

		for (u32 i = 0; i < registry.count; ++i)
		{
			const u32 callback = registry.entries + i * CALLBACK_STRIDE;
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

	static Status FindMenuPointer(ActiveMenu* active, const char** error)
	{
		*active = {};
		CallbackRegistry registry;
		const Status registry_status = ReadCallbackRegistry(&registry, error);
		active->callback_count = registry.count;
		if (registry_status != Status::Success)
			return registry_status == Status::MenuUnavailable ? Status::PointerUnavailable : registry_status;

		for (u32 i = 0; i < registry.count; ++i)
		{
			const u32 callback = registry.entries + i * CALLBACK_STRIDE;
			u32 owner_handle = 0;
			u32 owner = 0;
			u32 vtable = 0;
			u32 get_menu_item = 0;
			u32 update_position_absolute = 0;
			u32 action = 0;
			if (!GuestObjects::ReadWord(callback + CALLBACK_OWNER_OFFSET, &owner_handle) ||
				!GuestObjects::ResolveHandle(owner_handle, &owner) || owner == 0 ||
				!GuestObjects::ReadWord(owner, &vtable) || !GuestObjects::IsPlausibleAddress(vtable) ||
				!GuestObjects::ReadWord(vtable + GET_MENU_ITEM_VTABLE_OFFSET, &get_menu_item) ||
				!GuestObjects::ReadWord(
					vtable + UPDATE_POINTER_ABSOLUTE_VTABLE_OFFSET, &update_position_absolute) ||
				!GuestObjects::ReadWord(vtable + POINTER_ACTION_VTABLE_OFFSET, &action) ||
				get_menu_item != GET_MENU_ITEM ||
				update_position_absolute != UPDATE_POINTER_POSITION_ABSOLUTE ||
				action != MENU_POINTER_ACTION)
			{
				continue;
			}
			if (active->object != 0 && active->object != owner)
			{
				*error = "more than one menu-capable pointer owns active input callbacks";
				return Status::AmbiguousPointer;
			}
			active->object = owner;
		}

		if (active->object == 0)
		{
			*error = "no active menu-capable pointer owns input callbacks";
			return Status::PointerUnavailable;
		}
		return Status::Success;
	}

	static bool ReadFocus(const u32 menu, FocusState* focus)
	{
		*focus = {};
		return GuestObjects::ReadWord(menu + FOCUSED_ITEM_HANDLE_OFFSET, &focus->handle) &&
		       GuestObjects::ResolveHandle(focus->handle, &focus->object);
	}

	static bool ReadPointerFocus(const u32 pointer, FocusState* focus)
	{
		*focus = {};
		return GuestObjects::ReadWord(pointer + MENU_POINTER_FOCUS_HANDLE_OFFSET, &focus->handle) &&
		       GuestObjects::ResolveHandle(focus->handle, &focus->object);
	}

	static Status TranslateMotionStatus(const NativePointerMotion::Status status)
	{
		switch (status)
		{
			case NativePointerMotion::Status::Success:
				return Status::Success;
			case NativePointerMotion::Status::InvalidCoordinates:
				return Status::InvalidCoordinates;
			case NativePointerMotion::Status::PointerUnavailable:
				return Status::PointerUnavailable;
			case NativePointerMotion::Status::GuestMemoryError:
				return Status::GuestMemoryError;
			case NativePointerMotion::Status::ResolutionUnavailable:
				return Status::ResolutionUnavailable;
			case NativePointerMotion::Status::ShuttleFailure:
			default:
				return Status::ShuttleFailure;
		}
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

	static void InspectPointerOnCPUThread(PointerResult* result)
	{
		ActiveMenu active;
		result->status = FindMenuPointer(&active, &result->error);
		result->callback_count = active.callback_count;
		result->pointer = active.object;
		if (result->status != Status::Success)
			return;
		if (!ReadPointerFocus(result->pointer, &result->before))
		{
			result->status = Status::GuestMemoryError;
			result->error = "menu-capable pointer focus handle is invalid or unreadable";
		}
	}

	PointerResult InspectPointer()
	{
		PointerResult result;
		EECallShuttle::RunTransaction(
			[&result](EECallShuttle::Transaction&) { InspectPointerOnCPUThread(&result); });
		return result;
	}

	PointerResult MovePointer(const float normalized_x, const float normalized_y)
	{
		if (!NativePointerMotion::CoordinatesAreValid(normalized_x, normalized_y))
		{
			return {
				.status = Status::InvalidCoordinates,
				.error = "normalized coordinates must be finite values in 0..1",
			};
		}

		PointerResult result;
		EECallShuttle::RunTransaction(
			[&result, normalized_x, normalized_y](EECallShuttle::Transaction& transaction) {
				InspectPointerOnCPUThread(&result);
				if (result.status != Status::Success)
					return;

				const NativePointerMotion::Result motion = NativePointerMotion::MoveAbsolute(
					transaction, result.pointer, normalized_x, normalized_y);
				result.shuttle_status = motion.shuttle_status;
				result.screen_x = motion.screen_x;
				result.screen_y = motion.screen_y;
				result.observed_x = motion.observed_x;
				result.observed_y = motion.observed_y;
				result.staging_address = motion.staging_address;
				result.stack_restored = motion.stack_restored;
				result.elapsed_cycles = motion.elapsed_cycles;
				if (!motion.Succeeded())
				{
					result.status = TranslateMotionStatus(motion.status);
					result.error = motion.error;
					return;
				}

				result.handler = MENU_POINTER_CHECK;
				EECallShuttle::Request check_request{.function = result.handler};
				check_request.arguments[0] = result.pointer;
				const EECallShuttle::DeferredTicket ticket = transaction.QueueDeferred(check_request);
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
			});
		return result;
	}

	PointerResult ActivatePointer()
	{
		PointerResult result;
		EECallShuttle::RunTransaction([&result](EECallShuttle::Transaction& transaction) {
			InspectPointerOnCPUThread(&result);
			if (result.status != Status::Success)
				return;
			if (result.before.object == 0)
			{
				result.status = Status::FocusUnavailable;
				result.error = "menu-capable pointer is not focused on a menu item";
				return;
			}

			result.handler = MENU_POINTER_ACTION;
			EECallShuttle::Request request{.function = result.handler};
			request.arguments[0] = result.pointer;
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
		});
		return result;
	}
} // namespace AVPE::NativeMenuInput
