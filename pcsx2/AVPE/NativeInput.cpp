// AVP:E native keyboard/mouse bridge. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeInput.h"

#include "AVPE/GuestObjects.h"
#include "AVPE/NativePointerMotion.h"

#include <mutex>

namespace AVPE::NativeInput
{
	static constexpr u32 SET_INPUT_TYPE = 0x001B18E0;
	static constexpr u32 PRESS_MOUSE_PRIMARY = 0x001B52C0;
	static constexpr u32 RELEASE_MOUSE_PRIMARY = 0x001B52D0;
	static constexpr u32 PRESS_MOUSE_SECONDARY = 0x001B5300;
	static constexpr u32 RELEASE_MOUSE_SECONDARY = 0x001B5310;
	static constexpr u32 POINTER_SINGLETON = 0x00367720;
	static constexpr u32 INPUT_TYPE_OFFSET = 0x224;
	static constexpr u32 SELECTION_ARRAY_OFFSET = 0x1B0;
	static constexpr u32 SELECTED_OBJECT_OFFSET = 0xA8;
	static constexpr u32 CURRENT_COMMAND_ID_OFFSET = 0x460;
	static constexpr u32 MAX_SELECTION_COUNT = 32;

	static std::mutex s_button_mutex;
	static bool s_primary_pressed = false;
	static bool s_secondary_pressed = false;

	static Result Fail(const Status status, const char* error)
	{
		return {.status = status, .error = error};
	}

	static bool ReadLivePointer(u32* pointer)
	{
		return GuestObjects::ReadWord(POINTER_SINGLETON, pointer) && GuestObjects::IsPlausibleObject(*pointer);
	}

	static bool ReadSelection(const u32 pointer, SelectionState* state)
	{
		*state = {};
		u32 array = 0;
		u32 data = 0;
		if (!GuestObjects::ReadWord(pointer + SELECTION_ARRAY_OFFSET, &array) ||
			!GuestObjects::IsPlausibleAddress(array) || !GuestObjects::ReadWord(array, &data) ||
			!GuestObjects::ReadWord(array + sizeof(u32), &state->count) ||
			state->count > MAX_SELECTION_COUNT)
		{
			return false;
		}
		if (state->count == 0 || !GuestObjects::IsPlausibleAddress(data) ||
			!GuestObjects::ReadWord(data, &state->selected_mark) ||
			!GuestObjects::IsPlausibleObject(state->selected_mark) ||
			!GuestObjects::ReadWord(state->selected_mark + SELECTED_OBJECT_OFFSET,
				&state->selected_object) ||
			!GuestObjects::IsPlausibleObject(state->selected_object))
		{
			state->selected_mark = 0;
			state->selected_object = 0;
			return true;
		}
		return GuestObjects::ReadWord(
			state->selected_object + CURRENT_COMMAND_ID_OFFSET, &state->command_id);
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
			case NativePointerMotion::Status::ResolutionUnavailable:
				return Status::ResolutionUnavailable;
			case NativePointerMotion::Status::GuestMemoryError:
				return Status::GuestMemoryError;
			case NativePointerMotion::Status::ShuttleFailure:
			default:
				return Status::ShuttleFailure;
		}
	}

	static Result MoveOnCPUThread(
		EECallShuttle::Transaction& transaction, const float normalized_x, const float normalized_y)
	{
		u32 pointer = 0;
		if (!ReadLivePointer(&pointer))
		{
			return Fail(Status::PointerUnavailable, "live AVP:E pointer singleton is null or implausible");
		}

		EECallShuttle::Request selector_request{.function = SET_INPUT_TYPE};
		selector_request.arguments = {pointer, 1, 0, 0};
		const EECallShuttle::Result selector_call = transaction.Call(selector_request);
		if (!selector_call.Succeeded())
		{
			Result result = Fail(Status::ShuttleFailure, selector_call.error);
			result.shuttle_status = selector_call.status;
			return result;
		}
		u32 selector_mode = 0;
		if (!GuestObjects::ReadWord(pointer + INPUT_TYPE_OFFSET, &selector_mode) || selector_mode != 1)
			return Fail(Status::SelectorModeRejected, "game did not retain absolute pointer mode");

		const NativePointerMotion::Result motion =
			NativePointerMotion::MoveAbsolute(transaction, pointer, normalized_x, normalized_y);
		if (!motion.Succeeded())
		{
			Result result = Fail(TranslateMotionStatus(motion.status), motion.error);
			result.shuttle_status = motion.shuttle_status;
			result.pointer = pointer;
			result.staging_address = motion.staging_address;
			result.stack_restored = motion.stack_restored;
			return result;
		}

		return {
			.status = Status::Success,
			.shuttle_status = motion.shuttle_status,
			.screen_x = motion.screen_x,
			.screen_y = motion.screen_y,
			.observed_x = motion.observed_x,
			.observed_y = motion.observed_y,
			.pointer = pointer,
			.staging_address = motion.staging_address,
			.elapsed_cycles = selector_call.elapsed_cycles + motion.elapsed_cycles,
			.stack_restored = motion.stack_restored,
		};
	}

	Result MoveAbsolute(const float normalized_x, const float normalized_y)
	{
		if (!NativePointerMotion::CoordinatesAreValid(normalized_x, normalized_y))
		{
			return Fail(Status::InvalidCoordinates, "normalized coordinates must be finite values in 0..1");
		}

		Result result;
		EECallShuttle::RunTransaction(
			[&result, normalized_x, normalized_y](EECallShuttle::Transaction& transaction) {
				result = MoveOnCPUThread(transaction, normalized_x, normalized_y);
			});
		return result;
	}

	static ButtonResult FailButton(
		const Status status, const MouseButton button, const ButtonEdge edge, const char* error)
	{
		return {.status = status, .button = button, .edge = edge, .error = error};
	}

	static u32 HandlerFor(const MouseButton button, const ButtonEdge edge)
	{
		if (button == MouseButton::Primary)
			return edge == ButtonEdge::Press ? PRESS_MOUSE_PRIMARY : RELEASE_MOUSE_PRIMARY;
		return edge == ButtonEdge::Press ? PRESS_MOUSE_SECONDARY : RELEASE_MOUSE_SECONDARY;
	}

	ButtonResult ApplyButtonEdge(const MouseButton button, const ButtonEdge edge)
	{
		std::lock_guard lock(s_button_mutex);
		bool& pressed = button == MouseButton::Primary ? s_primary_pressed : s_secondary_pressed;
		if (pressed == (edge == ButtonEdge::Press))
		{
			return FailButton(Status::InvalidButtonEdge, button, edge,
				pressed ? "button is already pressed" : "button is not pressed");
		}

		ButtonResult result;
		EECallShuttle::RunTransaction([&result, button, edge](EECallShuttle::Transaction& transaction) {
			result.button = button;
			result.edge = edge;
			result.handler = HandlerFor(button, edge);
			if (!ReadLivePointer(&result.pointer))
			{
				result.status = Status::PointerUnavailable;
				result.error = "live AVP:E pointer singleton is null or implausible";
				return;
			}
			if (!ReadSelection(result.pointer, &result.before))
			{
				result.status = Status::GuestMemoryError;
				result.error = "game selection container is invalid or unreadable";
				return;
			}

			EECallShuttle::Request request{.function = result.handler};
			request.arguments[0] = result.pointer;
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
			if (!ReadSelection(result.pointer, &result.after))
			{
				result.status = Status::GuestMemoryError;
				result.error = "game selection container became invalid after mouse handler";
				return;
			}
			result.status = Status::Success;
		});
		if (result.Succeeded())
			pressed = edge == ButtonEdge::Press;
		return result;
	}

	void ResetAfterStateLoad()
	{
		std::lock_guard lock(s_button_mutex);
		s_primary_pressed = false;
		s_secondary_pressed = false;
	}
} // namespace AVPE::NativeInput
