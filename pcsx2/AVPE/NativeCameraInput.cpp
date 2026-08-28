// AVP:E native camera and minimap input. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeCameraInput.h"

#include "AVPE/GuestObjects.h"
#include "AVPE/NativeInputData.h"

#include <bit>
#include <cmath>

namespace AVPE::NativeCameraInput
{
	static constexpr u32 CAMERA_SINGLETON = 0x003676F0;
	static constexpr u32 MINIMAP_SINGLETON = 0x00367F40;
	static constexpr u32 POINTER_SINGLETON = 0x00367720;
	static constexpr u32 CAMERA_MOVE = 0x001AF140;
	static constexpr u32 CAMERA_ROTATE = 0x001AF240;
	static constexpr u32 CAMERA_ZOOM = 0x001AF480;

	static constexpr u32 CAMERA_DIRECTION_X_OFFSET = 0x13C;
	static constexpr u32 CAMERA_DIRECTION_Y_OFFSET = 0x140;
	static constexpr u32 CAMERA_DIRECTION_Z_OFFSET = 0x144;
	static constexpr u32 CAMERA_MOVE_X_OFFSET = 0x158;
	static constexpr u32 CAMERA_MOVE_Y_OFFSET = 0x15C;
	static constexpr u32 CAMERA_MODE_OFFSET = 0x238;
	static constexpr u32 POINTER_INPUT_TYPE_OFFSET = 0x224;
	static constexpr u32 MINIMAP_MODE_OFFSET = 0x24D;
	static constexpr u32 MINIMAP_POINTER_MODE_OFFSET = 0x24E;
	static constexpr u32 MINIMAP_CURSOR_X_OFFSET = 0xBC0;
	static constexpr u32 MINIMAP_CURSOR_Y_OFFSET = 0xBC4;
	static constexpr u32 MINIMAP_CURSOR_Z_OFFSET = 0xBC8;
	static constexpr u32 MINIMAP_CAMERA_X_OFFSET = 0x250;
	static constexpr u32 MINIMAP_CAMERA_Y_OFFSET = 0x254;

	static bool ReadFloat(const u32 address, float* value)
	{
		u32 bits = 0;
		if (!GuestObjects::ReadWord(address, &bits))
			return false;
		*value = std::bit_cast<float>(bits);
		return std::isfinite(*value);
	}

	static bool ReadByte(const u32 address, bool* value)
	{
		u32 word = 0;
		if (!GuestObjects::ReadWord(address & ~3u, &word))
			return false;
		*value = ((word >> ((address & 3u) * 8u)) & 0xffu) != 0;
		return true;
	}

	static bool ReadState(const u32 camera, const u32 minimap, State* state)
	{
		*state = {};
		if (!GuestObjects::ReadWord(POINTER_SINGLETON, &state->pointer))
			return false;
		if (state->pointer != 0 && GuestObjects::IsPlausibleObject(state->pointer) &&
			!GuestObjects::ReadWord(state->pointer + POINTER_INPUT_TYPE_OFFSET,
				&state->pointer_input_type))
		{
			return false;
		}
		state->camera = camera;
		if (!ReadFloat(camera + CAMERA_DIRECTION_X_OFFSET, &state->camera_direction_x) ||
			!ReadFloat(camera + CAMERA_DIRECTION_Y_OFFSET, &state->camera_direction_y) ||
			!ReadFloat(camera + CAMERA_DIRECTION_Z_OFFSET, &state->camera_direction_z) ||
			!ReadFloat(camera + CAMERA_MOVE_X_OFFSET, &state->camera_move_x) ||
			!ReadFloat(camera + CAMERA_MOVE_Y_OFFSET, &state->camera_move_y) ||
			!ReadByte(camera + CAMERA_MODE_OFFSET, &state->camera_mode))
		{
			return false;
		}

		state->minimap = minimap;
		if (minimap == 0)
			return true;
		return ReadFloat(minimap + MINIMAP_CURSOR_X_OFFSET, &state->minimap_cursor_x) &&
		       ReadFloat(minimap + MINIMAP_CURSOR_Y_OFFSET, &state->minimap_cursor_y) &&
		       ReadFloat(minimap + MINIMAP_CURSOR_Z_OFFSET, &state->minimap_cursor_z) &&
		       ReadFloat(minimap + MINIMAP_CAMERA_X_OFFSET, &state->minimap_camera_x) &&
		       ReadFloat(minimap + MINIMAP_CAMERA_Y_OFFSET, &state->minimap_camera_y) &&
		       ReadByte(minimap + MINIMAP_MODE_OFFSET, &state->minimap_mode) &&
		       ReadByte(minimap + MINIMAP_POINTER_MODE_OFFSET, &state->minimap_pointer_mode);
	}

	static Result Fail(const Action action, const Status status, const char* error)
	{
		return {.status = status, .action = action, .error = error};
	}

	static u32 FunctionFor(const Action action)
	{
		switch (action)
		{
			case Action::Move:
				return CAMERA_MOVE;
			case Action::Rotate:
				return CAMERA_ROTATE;
			case Action::Zoom:
				return CAMERA_ZOOM;
		}
		return 0;
	}

	static Result ApplyOnCPUThread(
		EECallShuttle::Transaction& transaction, const Action action, const float x, const float y)
	{
		u32 camera = 0;
		if (!GuestObjects::ReadWord(CAMERA_SINGLETON, &camera) ||
			!GuestObjects::IsPlausibleObject(camera))
		{
			return Fail(action, Status::CameraUnavailable,
				"live AVP:E camera singleton is null or implausible");
		}

		u32 minimap = 0;
		if (!GuestObjects::ReadWord(MINIMAP_SINGLETON, &minimap))
			return Fail(action, Status::GuestMemoryError, "AVP:E minimap singleton is unreadable");
		if (minimap != 0 && !GuestObjects::IsPlausibleObject(minimap))
			return Fail(action, Status::GuestMemoryError, "AVP:E minimap singleton is implausible");

		Result result = {.status = Status::ShuttleFailure, .action = action, .input_x = x, .input_y = y};
		if (!ReadState(camera, minimap, &result.before))
			return Fail(action, Status::GuestMemoryError, "camera or minimap state is unreadable");

		const std::array<u8, 8> input_data = NativeInputData::EncodeFloatPair(x, y);
		EECallShuttle::Request request{.function = FunctionFor(action)};
		request.arguments[0] = camera;
		const EECallShuttle::Result call = transaction.CallWithStackBuffer(request, 1, input_data);
		result.shuttle_status = call.status;
		result.staging_address = call.staging_address;
		result.elapsed_cycles = call.elapsed_cycles;
		result.stack_restored = call.stack_restored;
		if (!call.Succeeded())
		{
			result.status = call.status == EECallShuttle::Status::GuestMemoryError ?
			                    Status::GuestMemoryError :
			                    Status::ShuttleFailure;
			result.error = call.error;
			return result;
		}
		if (!ReadState(camera, minimap, &result.after))
		{
			result.status = Status::GuestMemoryError;
			result.error = "camera or minimap state became unreadable after input";
			return result;
		}
		result.status = Status::Success;
		return result;
	}

	Result Apply(const Action action, const float x, const float y)
	{
		if (!std::isfinite(x) || !std::isfinite(y))
			return Fail(action, Status::InvalidInput, "camera input must be finite");
		Result result;
		EECallShuttle::RunTransaction([&result, action, x, y](EECallShuttle::Transaction& transaction) {
			result = ApplyOnCPUThread(transaction, action, x, y);
		});
		return result;
	}
} // namespace AVPE::NativeCameraInput
