// Shared AVP:E absolute-pointer motion. Fork-local; not for upstream PCSX2.

#include "AVPE/NativePointerMotion.h"

#include "AVPE/GuestObjects.h"
#include "AVPE/NativeInputData.h"

#include <array>
#include <cmath>

namespace AVPE::NativePointerMotion
{
	static constexpr u32 GET_RESOLUTION = 0x00137B30;
	static constexpr u32 UPDATE_POSITION_ABSOLUTE = 0x0012EAB0;
	static constexpr u32 POSITION_X_OFFSET = 0x194;
	static constexpr u32 POSITION_Y_OFFSET = 0x198;
	static constexpr u32 PHYSICAL_POSITION_X_OFFSET = 0x40;
	static constexpr u32 PHYSICAL_POSITION_Y_OFFSET = 0x44;
	static constexpr s32 MAX_PLAUSIBLE_RESOLUTION = 8192;

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

	bool CoordinatesAreValid(const float normalized_x, const float normalized_y)
	{
		return std::isfinite(normalized_x) && std::isfinite(normalized_y) &&
		       normalized_x >= 0.0f && normalized_x <= 1.0f &&
		       normalized_y >= 0.0f && normalized_y <= 1.0f;
	}

	bool ReadPosition(const u32 pointer, float* const x, float* const y)
	{
		return x && y && GuestObjects::IsPlausibleObject(pointer) &&
		       ReadFloat(pointer + POSITION_X_OFFSET, x) && ReadFloat(pointer + POSITION_Y_OFFSET, y);
	}

	bool ReadPhysicalPosition(const u32 pointer, float* const x, float* const y)
	{
		return x && y && GuestObjects::IsPlausibleObject(pointer) &&
		       ReadFloat(pointer + PHYSICAL_POSITION_X_OFFSET, x) &&
		       ReadFloat(pointer + PHYSICAL_POSITION_Y_OFFSET, y);
	}

	static Result ResolveTarget(EECallShuttle::Transaction& transaction, const u32 pointer,
		const float normalized_x, const float normalized_y, float* const screen_x, float* const screen_y)
	{
		if (!CoordinatesAreValid(normalized_x, normalized_y))
			return Fail(Status::InvalidCoordinates, "normalized coordinates must be finite values in 0..1");
		if (!GuestObjects::IsPlausibleObject(pointer))
			return Fail(Status::PointerUnavailable, "game pointer object is null or implausible");

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

		const float width = static_cast<float>(bounds[2] - bounds[0]);
		const float height = static_cast<float>(bounds[3] - bounds[1]);
		*screen_x = static_cast<float>(bounds[0]) + normalized_x * (width - 1.0f);
		*screen_y = static_cast<float>(bounds[1]) + normalized_y * (height - 1.0f);
		return {
			.status = Status::Success,
			.shuttle_status = resolution_call.status,
			.screen_x = *screen_x,
			.screen_y = *screen_y,
			.pointer = pointer,
			.elapsed_cycles = resolution_call.elapsed_cycles,
			.stack_restored = resolution_call.stack_restored,
		};
	}

	Result PrepareGAvPPointerRelativeInput(EECallShuttle::Transaction& transaction, const u32 pointer,
		const float normalized_x, const float normalized_y, RelativeInput* const input)
	{
		if (!input)
			return Fail(Status::GuestMemoryError, "relative input destination is null");

		float screen_x = 0.0f;
		float screen_y = 0.0f;
		Result result = ResolveTarget(transaction, pointer, normalized_x, normalized_y, &screen_x, &screen_y);
		if (!result.Succeeded())
			return result;
		if (!ReadPhysicalPosition(pointer, &result.observed_x, &result.observed_y))
		{
			return Fail(Status::GuestMemoryError, "current game physical pointer fields are not readable floats");
		}

		// GAvPPointer applies this vector before and inside its GfsPointer base
		// call, so one normal callback advances each component twice by 10000.
		constexpr float kGAvPPointerRelativeScale = 20000.0f;
		input->x = (screen_x - result.observed_x) / kGAvPPointerRelativeScale;
		input->y = (screen_y - result.observed_y) / kGAvPPointerRelativeScale;
		return result;
	}

	Result MoveAbsolute(EECallShuttle::Transaction& transaction, const u32 pointer,
		const float normalized_x, const float normalized_y)
	{
		float screen_x = 0.0f;
		float screen_y = 0.0f;
		const Result target = ResolveTarget(transaction, pointer, normalized_x, normalized_y, &screen_x, &screen_y);
		if (!target.Succeeded())
			return target;
		const std::array<u8, 8> input_data = NativeInputData::EncodeFloatPair(screen_x, screen_y);

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
			result.return_pc = update_call.return_pc;
			result.stopped_pc = update_call.stopped_pc;
			result.last_avpe_text_pc = update_call.last_avpe_text_pc;
			result.elapsed_cycles = target.elapsed_cycles + update_call.elapsed_cycles;
			result.stack_restored = update_call.stack_restored;
			return result;
		}

		float observed_x = 0.0f;
		float observed_y = 0.0f;
		if (!ReadPosition(pointer, &observed_x, &observed_y))
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
			.return_pc = update_call.return_pc,
			.stopped_pc = update_call.stopped_pc,
			.last_avpe_text_pc = update_call.last_avpe_text_pc,
			.elapsed_cycles = target.elapsed_cycles + update_call.elapsed_cycles,
			.stack_restored = update_call.stack_restored,
		};
	}
} // namespace AVPE::NativePointerMotion
