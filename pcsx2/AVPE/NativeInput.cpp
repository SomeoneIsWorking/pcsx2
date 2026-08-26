// AVP:E native keyboard/mouse bridge. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeInput.h"

#include "AVPE/GuestObjects.h"

#include <array>
#include <bit>
#include <cmath>
#include <mutex>

namespace AVPE::NativeInput
{
	static constexpr u32 GET_RESOLUTION = 0x00137B30;
	static constexpr u32 SET_INPUT_TYPE = 0x001B18E0;
	static constexpr u32 UPDATE_POSITION_ABSOLUTE = 0x0012EAB0;
	static constexpr u32 PRESS_MOUSE_PRIMARY = 0x001B52C0;
	static constexpr u32 RELEASE_MOUSE_PRIMARY = 0x001B52D0;
	static constexpr u32 PRESS_MOUSE_SECONDARY = 0x001B5300;
	static constexpr u32 RELEASE_MOUSE_SECONDARY = 0x001B5310;
	static constexpr u32 POINTER_SINGLETON = 0x00367720;
	static constexpr u32 INPUT_TYPE_OFFSET = 0x224;
	static constexpr u32 POSITION_X_OFFSET = 0x194;
	static constexpr u32 POSITION_Y_OFFSET = 0x198;
	static constexpr u32 SELECTION_ARRAY_OFFSET = 0x1B0;
	static constexpr u32 SELECTED_OBJECT_OFFSET = 0xA8;
	static constexpr u32 CURRENT_COMMAND_ID_OFFSET = 0x460;
	static constexpr s32 MAX_PLAUSIBLE_RESOLUTION = 8192;
	static constexpr u32 MAX_SELECTION_COUNT = 32;

	static std::mutex s_button_mutex;
	static bool s_primary_pressed = false;
	static bool s_secondary_pressed = false;

	static Result Fail(const Status status, const char* error)
	{
		return {.status = status, .error = error};
	}

	static bool ReadFloat(const u32 address, float* value)
	{
		u32 bits = 0;
		if (!GuestObjects::ReadWord(address, &bits))
			return false;
		*value = std::bit_cast<float>(bits);
		return std::isfinite(*value);
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

	static std::array<u8, 8> EncodeCoordinates(const float x, const float y)
	{
		const u32 words[] = {std::bit_cast<u32>(x), std::bit_cast<u32>(y)};
		std::array<u8, 8> bytes{};
		for (u32 word_index = 0; word_index < 2; ++word_index)
		{
			for (u32 byte_index = 0; byte_index < 4; ++byte_index)
			{
				bytes[word_index * 4 + byte_index] =
					static_cast<u8>(words[word_index] >> (byte_index * 8));
			}
		}
		return bytes;
	}

	static Result MoveOnCPUThread(
		EECallShuttle::Transaction& transaction, const float normalized_x, const float normalized_y)
	{
		const EECallShuttle::Result resolution_call = transaction.Call({.function = GET_RESOLUTION});
		if (!resolution_call.Succeeded() || resolution_call.v0 > 0xffffffffULL)
		{
			Result result = Fail(Status::ResolutionUnavailable,
				resolution_call.Succeeded() ? "game returned an invalid resolution pointer" : resolution_call.error);
			result.shuttle_status = resolution_call.status;
			return result;
		}

		const u32 resolution_address = static_cast<u32>(resolution_call.v0);
		std::array<s32, 4> bounds{};
		for (u32 i = 0; i < bounds.size(); ++i)
		{
			u32 word = 0;
			if (!GuestObjects::ReadWord(resolution_address + i * sizeof(u32), &word))
				return Fail(Status::ResolutionUnavailable, "game resolution bounds are not readable");
			bounds[i] = static_cast<s32>(word);
		}
		if (bounds[0] < 0 || bounds[1] < 0 || bounds[2] <= bounds[0] || bounds[3] <= bounds[1] ||
			bounds[2] > MAX_PLAUSIBLE_RESOLUTION || bounds[3] > MAX_PLAUSIBLE_RESOLUTION)
		{
			return Fail(Status::ResolutionUnavailable, "game returned implausible resolution bounds");
		}

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

		const float width = static_cast<float>(bounds[2] - bounds[0]);
		const float height = static_cast<float>(bounds[3] - bounds[1]);
		const float screen_x = static_cast<float>(bounds[0]) + normalized_x * (width - 1.0f);
		const float screen_y = static_cast<float>(bounds[1]) + normalized_y * (height - 1.0f);
		const std::array<u8, 8> input_data = EncodeCoordinates(screen_x, screen_y);

		EECallShuttle::Request update_request{.function = UPDATE_POSITION_ABSOLUTE};
		update_request.arguments[0] = pointer;
		const EECallShuttle::Result update_call =
			transaction.CallWithStackBuffer(update_request, 1, input_data);
		if (!update_call.Succeeded())
		{
			Result result = Fail(update_call.status == EECallShuttle::Status::GuestMemoryError ?
									 Status::GuestMemoryError :
									 Status::ShuttleFailure,
				update_call.error);
			result.shuttle_status = update_call.status;
			result.pointer = pointer;
			result.staging_address = update_call.staging_address;
			result.stack_restored = update_call.stack_restored;
			return result;
		}

		float observed_x = 0.0f;
		float observed_y = 0.0f;
		if (!ReadFloat(pointer + POSITION_X_OFFSET, &observed_x) ||
			!ReadFloat(pointer + POSITION_Y_OFFSET, &observed_y))
		{
			return Fail(Status::GuestMemoryError, "updated game pointer fields are not readable floats");
		}

		return {
			.status = Status::Success,
			.shuttle_status = update_call.status,
			.screen_x = screen_x,
			.screen_y = screen_y,
			.observed_x = observed_x,
			.observed_y = observed_y,
			.pointer = pointer,
			.staging_address = update_call.staging_address,
			.elapsed_cycles = resolution_call.elapsed_cycles + selector_call.elapsed_cycles + update_call.elapsed_cycles,
			.stack_restored = update_call.stack_restored,
		};
	}

	Result MoveAbsolute(const float normalized_x, const float normalized_y)
	{
		if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y) ||
			normalized_x < 0.0f || normalized_x > 1.0f || normalized_y < 0.0f || normalized_y > 1.0f)
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
