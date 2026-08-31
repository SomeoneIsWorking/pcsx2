// AVP:E native menu-action bridge. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMenuInput.h"

#include "AVPE/GuestObjects.h"
#include "AVPE/NativeInputDispatch.h"
#include "AVPE/NativePointerMotion.h"
#include <array>

namespace AVPE::NativeMenuInput
{
	static constexpr u32 INPUT_DEVICE_SINGLETON = 0x00366E68;
	static constexpr u32 MISSION_GOALS_MENU_SINGLETON = 0x00367C04;
	static constexpr u32 MISSION_GOALS_MENU_VTABLE = 0x00342570;
	static constexpr u32 MISSION_GOALS_EXIT_VTABLE = 0x00342370;
	static constexpr u32 CALLBACK_ARRAY_OFFSET = 0x48;
	static constexpr u32 FOCUSED_ITEM_HANDLE_OFFSET = 0x26C;
	static constexpr u32 CALLBACK_STRIDE = 0x18;
	static constexpr u32 CALLBACK_OWNER_OFFSET = 0x08;
	static constexpr u32 CALLBACK_FUNCTION_OFFSET = 0x14;
	static constexpr u32 MENU_INPUT = 0x00125330;
	static constexpr u32 MENU_ITEM_FOCUS = 0x00120B70;
	static constexpr u32 MENU_ITEM_FOCUS_VTABLE_OFFSET = 0xB8;
	static constexpr u32 MENU_ITEM_ACTION_OFFSET = 0x110;
	static constexpr u32 FIRST_CHILD_OFFSET = 0x08;
	static constexpr u32 NEXT_SIBLING_OFFSET = 0x10;
	static constexpr u32 MENU_CANCEL_VTABLE_OFFSET = 0xFC;
	static constexpr u32 MENU_POINTER_FOCUS_HANDLE_OFFSET = 0x1AC;
	static constexpr u32 MENU_POINTER_ACTION = 0x0012EBB0;
	static constexpr u32 GET_MENU_ITEM = 0x0012E8C0;
	static constexpr u32 UPDATE_POINTER_POSITION_ABSOLUTE = 0x0012EAB0;
	static constexpr u32 GET_MENU_ITEM_VTABLE_OFFSET = 0xD4;
	static constexpr u32 UPDATE_POINTER_ABSOLUTE_VTABLE_OFFSET = 0xDC;
	static constexpr u32 POINTER_ACTION_VTABLE_OFFSET = 0xE0;
	static constexpr u32 POINTER_CHECK_VTABLE_OFFSET = 0xCC;
	static constexpr u32 POINTER_UPDATE_VTABLE_FUNCTION = 0x001B51A0;
	static constexpr u32 MAX_CALLBACK_COUNT = 256;
	static constexpr u32 MAX_MISSION_GOALS_OBJECTS = 256;
	static constexpr std::array<u32, 6> MENU_CALLBACKS = {
		0x00124BD0,
		0x00124BE0,
		0x00124BF0,
		0x00124C00,
		0x00124C10,
		0x00125230,
	};
	static constexpr std::array<u32, 5> MENU_ACTION_CALLBACKS = {
		0x00124C10,
		0x00124C00,
		0x00124BF0,
		0x00124BE0,
		0x00124BD0,
	};

	struct ActiveMenu
	{
		u32 object = 0;
		u32 conflicting_object = 0;
		u32 callback_count = 0;
		Source source = Source::None;
		u32 callback = 0;
		u32 pointer_check = 0;
		std::array<u32, MENU_ACTION_CALLBACKS.size()> action_callbacks{};
	};

	static u32 CallbackFunctionForAction(const Action action)
	{
		const size_t index = static_cast<size_t>(action);
		return index < MENU_ACTION_CALLBACKS.size() ? MENU_ACTION_CALLBACKS[index] : 0;
	}

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

	static void RecordMenuActionCallback(ActiveMenu* active, const u32 callback, const u32 function)
	{
		for (size_t index = 0; index < MENU_ACTION_CALLBACKS.size(); ++index)
		{
			if (MENU_ACTION_CALLBACKS[index] == function && active->action_callbacks[index] == 0)
			{
				active->action_callbacks[index] = callback;
				return;
			}
		}
	}

	static u32 AnyMenuActionCallback(const ActiveMenu& active)
	{
		for (const u32 callback : active.action_callbacks)
		{
			if (callback != 0)
				return callback;
		}
		return 0;
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
		if (registry_status != Status::Success && registry_status != Status::MenuUnavailable)
			return registry_status;

		if (registry_status == Status::Success)
		{
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
					active->conflicting_object = owner;
					active->source = Source::CallbackRegistry;
					*error = "more than one game menu owns active navigation callbacks";
					return Status::AmbiguousMenu;
				}
				active->object = owner;
				RecordMenuActionCallback(active, callback, function);
			}
		}

		u32 mission_goals_menu = 0;
		u32 mission_goals_vtable = 0;
		if (active->object == 0 &&
			(!GuestObjects::ReadWord(MISSION_GOALS_MENU_SINGLETON, &mission_goals_menu) ||
				(mission_goals_menu != 0 &&
					!GuestObjects::ReadWord(mission_goals_menu, &mission_goals_vtable))))
		{
			*error = "mission-goals menu identity is unreadable";
			return Status::GuestMemoryError;
		}

		active->source = IdentifyMenuSource(active->object, mission_goals_menu, mission_goals_vtable);
		if (active->source == Source::CallbackRegistry)
			return Status::Success;
		if (active->source == Source::MissionGoalsLoad)
		{
			active->object = mission_goals_menu;
			return Status::Success;
		}
		if (mission_goals_menu != 0)
		{
			*error = "mission-goals menu identity does not match the grounded class";
			return Status::GuestMemoryError;
		}
		*error = "no active game menu owns navigation callbacks";
		return Status::MenuUnavailable;
	}

	Source IdentifyMenuSource(
		const u32 callback_menu, const u32 mission_goals_menu, const u32 mission_goals_vtable)
	{
		if (callback_menu != 0)
			return Source::CallbackRegistry;
		if (mission_goals_menu != 0 && mission_goals_vtable == MISSION_GOALS_MENU_VTABLE)
			return Source::MissionGoalsLoad;
		return Source::None;
	}

	const char* SourceName(const Source source)
	{
		switch (source)
		{
			case Source::CallbackRegistry:
				return "callback-registry";
			case Source::MissionGoalsLoad:
				return "mission-goals-load";
			case Source::None:
			default:
				return "none";
		}
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
			u32 pointer_check = 0;
			std::array<u32, 3> member{};
			u32 pointer_update = 0;
			if (!GuestObjects::ReadWord(callback + CALLBACK_OWNER_OFFSET, &owner_handle) ||
				!GuestObjects::ResolveHandle(owner_handle, &owner) || owner == 0 ||
				!GuestObjects::ReadWord(owner, &vtable) || !GuestObjects::IsPlausibleAddress(vtable) ||
				!GuestObjects::ReadWord(vtable + GET_MENU_ITEM_VTABLE_OFFSET, &get_menu_item) ||
				!GuestObjects::ReadWord(
					vtable + UPDATE_POINTER_ABSOLUTE_VTABLE_OFFSET, &update_position_absolute) ||
				!GuestObjects::ReadWord(vtable + POINTER_ACTION_VTABLE_OFFSET, &action) ||
				!GuestObjects::ReadWord(vtable + POINTER_CHECK_VTABLE_OFFSET, &pointer_check) ||
				!GuestObjects::ReadBytes(callback + 0x0C, member.data(), sizeof(member)) ||
				member[2] != 0 || member[1] == 0 || (member[1] & 3) != 0 ||
				!GuestObjects::ReadWord(vtable + member[1], &pointer_update) ||
				get_menu_item != GET_MENU_ITEM ||
				update_position_absolute != UPDATE_POINTER_POSITION_ABSOLUTE ||
				action != MENU_POINTER_ACTION || !GuestObjects::IsPlausibleAddress(pointer_check) ||
				pointer_update != POINTER_UPDATE_VTABLE_FUNCTION)
			{
				continue;
			}
			if (active->object != 0 && active->object != owner)
			{
				*error = "more than one menu-capable pointer owns active input callbacks";
				return Status::AmbiguousPointer;
			}
			active->object = owner;
			active->callback = callback;
			active->pointer_check = pointer_check;
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
		       GuestObjects::ResolveHandle(focus->handle, &focus->object) &&
		       GuestObjects::ReadWord(focus->object, &focus->vtable) &&
		       GuestObjects::IsPlausibleAddress(focus->vtable);
	}

	static bool ReadObjectVtable(const u32 object, u32* vtable)
	{
		return GuestObjects::IsPlausibleObject(object) &&
		       GuestObjects::ReadWord(object, vtable) &&
		       GuestObjects::IsPlausibleAddress(*vtable);
	}

	static Status FindMissionGoalsExitItem(const u32 menu, u32* exit_item, const char** error)
	{
		*exit_item = 0;
		std::array<u32, MAX_MISSION_GOALS_OBJECTS> pending{};
		std::array<u32, MAX_MISSION_GOALS_OBJECTS> visited{};
		u32 pending_count = 0;
		u32 visited_count = 0;
		u32 first_child = 0;
		if (!GuestObjects::ReadWord(menu + FIRST_CHILD_OFFSET, &first_child))
		{
			*error = "mission-goals child list is unreadable";
			return Status::GuestMemoryError;
		}
		if (first_child != 0)
			pending[pending_count++] = first_child;

		while (pending_count != 0)
		{
			const u32 object = pending[--pending_count];
			bool already_visited = false;
			for (u32 i = 0; i < visited_count; ++i)
				already_visited = already_visited || visited[i] == object;
			if (already_visited)
				continue;
			if (visited_count >= visited.size() || !GuestObjects::IsPlausibleObject(object))
			{
				*error = "mission-goals object tree is invalid or exceeds its bound";
				return Status::GuestMemoryError;
			}
			visited[visited_count++] = object;

			u32 vtable = 0;
			u32 child = 0;
			u32 sibling = 0;
			if (!GuestObjects::ReadWord(object, &vtable) ||
				!GuestObjects::ReadWord(object + FIRST_CHILD_OFFSET, &child) ||
				!GuestObjects::ReadWord(object + NEXT_SIBLING_OFFSET, &sibling))
			{
				*error = "mission-goals object tree is unreadable";
				return Status::GuestMemoryError;
			}
			if (vtable == MISSION_GOALS_EXIT_VTABLE)
			{
				if (*exit_item != 0 && *exit_item != object)
				{
					*error = "more than one mission-goals exit item is active";
					return Status::AmbiguousMenu;
				}
				*exit_item = object;
			}

			if ((child != 0 && pending_count >= pending.size()) ||
				(sibling != 0 && pending_count + (child != 0 ? 1 : 0) >= pending.size()))
			{
				*error = "mission-goals object traversal exceeds its bound";
				return Status::GuestMemoryError;
			}
			if (sibling != 0)
				pending[pending_count++] = sibling;
			if (child != 0)
				pending[pending_count++] = child;
		}

		if (*exit_item == 0)
		{
			*error = "mission-goals exit item is not available";
			return Status::FocusUnavailable;
		}
		return Status::Success;
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

	static bool ReadFocusedItemAction(const u32 item, u32* action)
	{
		return GuestObjects::IsPlausibleObject(item) &&
		       GuestObjects::ReadWord(item + MENU_ITEM_ACTION_OFFSET, action);
	}

	static void InspectOnCPUThread(Result* result, ActiveMenu* active_result = nullptr)
	{
		ActiveMenu active;
		result->status = FindActiveMenu(&active, &result->error);
		result->callback_count = active.callback_count;
		result->menu = active.object;
		result->conflicting_menu = active.conflicting_object;
		result->source = active.source;
		if ((result->menu != 0 && !ReadObjectVtable(result->menu, &result->menu_vtable)) ||
			(result->conflicting_menu != 0 &&
				!ReadObjectVtable(result->conflicting_menu, &result->conflicting_menu_vtable)))
		{
			result->status = Status::GuestMemoryError;
			result->error = "active game menu vtable is invalid or unreadable";
			return;
		}
		if (result->status != Status::Success)
			return;
		if (active_result)
			*active_result = active;
		if (result->source == Source::MissionGoalsLoad)
			result->status = FindMissionGoalsExitItem(result->menu, &result->action_target, &result->error);
		else
		{
			if (!ReadFocus(result->menu, &result->before))
			{
				result->status = Status::GuestMemoryError;
				result->error = "focused game menu item is invalid or unreadable";
				return;
			}
			result->action_target = result->before.object;
		}
		if (result->status == Status::Success)
			result->focused_item_action_valid =
				ReadFocusedItemAction(result->action_target, &result->focused_item_action);
	}

	static bool AcceptCallResult(Result* result, const EECallShuttle::Result& call)
	{
		result->shuttle_status = call.status;
		result->elapsed_cycles += call.elapsed_cycles;
		result->stopped_pc = call.stopped_pc;
		result->last_avpe_text_pc = call.last_avpe_text_pc;
		result->stack_restored = result->stack_restored && call.stack_restored;
		if (call.Succeeded())
			return true;
		result->status = call.status == EECallShuttle::Status::GuestMemoryError ?
		                     Status::GuestMemoryError :
		                     Status::ShuttleFailure;
		result->error = call.error;
		return false;
	}

	static bool PrepareMissionGoalsActivation(
		Result* result, EECallShuttle::Transaction& transaction, const Action action)
	{
		if (result->source != Source::MissionGoalsLoad || action != Action::Activate)
			return true;

		u32 exit_vtable = 0;
		u32 focus_handler = 0;
		if (!GuestObjects::ReadWord(result->action_target, &exit_vtable) ||
			!GuestObjects::ReadWord(exit_vtable + MENU_ITEM_FOCUS_VTABLE_OFFSET, &focus_handler) ||
			focus_handler != MENU_ITEM_FOCUS)
		{
			result->status = Status::GuestMemoryError;
			result->error = "mission-goals exit item has an unexpected focus handler";
			return false;
		}

		EECallShuttle::Request focus_request{.function = focus_handler};
		focus_request.arguments = {
			result->action_target,
			1,
			0,
			0,
		};
		const EECallShuttle::Result focus_call = transaction.Call(focus_request);
		if (!AcceptCallResult(result, focus_call))
			return false;
		if (!ReadFocus(result->menu, &result->before) || result->before.object != result->action_target)
		{
			result->status = Status::FocusUnavailable;
			result->error = "mission-goals exit focus did not resolve to the grounded object";
			return false;
		}
		return true;
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
			ActiveMenu active;
			InspectOnCPUThread(&result, &active);
			if (result.status != Status::Success)
				return;
			if (!PrepareMissionGoalsActivation(&result, transaction, action))
				return;
			if (result.source == Source::CallbackRegistry && action != Action::Cancel)
			{
				u32 callback = active.action_callbacks[static_cast<size_t>(action)];
				result.handler = CallbackFunctionForAction(action);
				if (callback == 0)
					callback = AnyMenuActionCallback(active);
				if (callback == 0 || result.handler == 0)
				{
					result.status = Status::GuestMemoryError;
					result.error = "active menu has no exact registered callback for this action";
					return;
				}
				const NativeInputDispatch::Result queue = NativeInputDispatch::QueueMenuAction(
					{.menu = result.menu, .callback = callback, .function = result.handler});
				result.dispatch_action_id = queue.id;
				result.deferred = queue.Succeeded();
				if (!queue.Succeeded())
				{
					result.status = queue.status == NativeInputDispatch::Status::Busy ?
					                    Status::ShuttleFailure :
					                    Status::GuestMemoryError;
					result.error = queue.error;
					return;
				}
				result.status = Status::Success;
				return;
			}

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
			if (result.source == Source::MissionGoalsLoad && action == Action::Activate)
			{
				// This runs reentrantly while NativeHostYield is observing the modal's
				// loop PC. A deferred call could mistake that original PC for its return.
				const EECallShuttle::Result call = transaction.Call(request);
				if (!AcceptCallResult(&result, call))
					return;
				result.status = Status::Success;
				return;
			}
			// Callback-registry menus run under the ordinary game scheduler. Even a
			// directional focus change can enter AVP:E's SIF-backed audio work, so
			// it must not monopolize the CPU in a synchronous shuttle call.
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
			if (!AcceptCallResult(&result, call))
				return;
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
		result->callback = active.callback;
		result->handler = active.pointer_check;
		if (result->status != Status::Success)
			return;
		if (!ReadPointerFocus(result->pointer, &result->before))
		{
			result->status = Status::GuestMemoryError;
			result->error = "menu-capable pointer focus handle is invalid or unreadable";
			return;
		}
		result->focused_item_action_valid =
			ReadFocusedItemAction(result->before.object, &result->focused_item_action);
		if (!NativePointerMotion::ReadPosition(result->pointer, &result->observed_x, &result->observed_y))
		{
			result->status = Status::GuestMemoryError;
			result->error = "menu-capable pointer position is invalid or unreadable";
			return;
		}
		if (!NativePointerMotion::ReadPhysicalPosition(result->pointer, &result->menu_x, &result->menu_y))
		{
			result->status = Status::GuestMemoryError;
			result->error = "menu-capable pointer physical position is invalid or unreadable";
			return;
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
				result.return_pc = motion.return_pc;
				result.stopped_pc = motion.stopped_pc;
				result.last_avpe_text_pc = motion.last_avpe_text_pc;
				result.stack_restored = motion.stack_restored;
				result.elapsed_cycles = motion.elapsed_cycles;
				if (!motion.Succeeded())
				{
					result.status = TranslateMotionStatus(motion.status);
					result.error = motion.error;
					return;
				}

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

	PointerResult MovePointerThroughDispatch(const float normalized_x, const float normalized_y)
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

				NativePointerMotion::RelativeInput relative;
				const NativePointerMotion::Result motion = NativePointerMotion::PrepareGAvPPointerRelativeInput(
					transaction, result.pointer, normalized_x, normalized_y, &relative);
				result.shuttle_status = motion.shuttle_status;
				result.screen_x = motion.screen_x;
				result.screen_y = motion.screen_y;
				result.observed_x = motion.observed_x;
				result.observed_y = motion.observed_y;
				result.elapsed_cycles = motion.elapsed_cycles;
				if (!motion.Succeeded())
				{
					result.status = TranslateMotionStatus(motion.status);
					result.error = motion.error;
					return;
				}

				const NativeInputDispatch::Result queue = NativeInputDispatch::QueuePointerMotion(
					{.pointer = result.pointer,
						.callback = result.callback,
						.x = relative.x,
						.y = relative.y});
				result.dispatch_pointer_id = queue.id;
				result.deferred = queue.Succeeded();
				if (!queue.Succeeded())
				{
					result.status = queue.status == NativeInputDispatch::Status::Busy ?
				                        Status::ShuttleFailure :
				                        Status::GuestMemoryError;
					result.error = queue.error;
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
