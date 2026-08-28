// AVP:E native camera and minimap input. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/EECallShuttle.h"

namespace AVPE::NativeCameraInput
{
	enum class Action : u8
	{
		Move,
		Rotate,
		Zoom,
	};

	enum class Status : u8
	{
		Success,
		InvalidInput,
		CameraUnavailable,
		GuestMemoryError,
		ShuttleFailure,
	};

	struct State
	{
		u32 pointer = 0;
		u32 pointer_input_type = 0;
		u32 camera = 0;
		float camera_direction_x = 0.0f;
		float camera_direction_y = 0.0f;
		float camera_direction_z = 0.0f;
		float camera_move_x = 0.0f;
		float camera_move_y = 0.0f;
		bool camera_mode = false;
		u32 minimap = 0;
		float minimap_cursor_x = 0.0f;
		float minimap_cursor_y = 0.0f;
		float minimap_cursor_z = 0.0f;
		float minimap_camera_x = 0.0f;
		float minimap_camera_y = 0.0f;
		bool minimap_mode = false;
		bool minimap_pointer_mode = false;
	};

	struct Result
	{
		Status status = Status::ShuttleFailure;
		EECallShuttle::Status shuttle_status = EECallShuttle::Status::Interrupted;
		Action action = Action::Move;
		float input_x = 0.0f;
		float input_y = 0.0f;
		State before;
		State after;
		u32 staging_address = 0;
		u64 elapsed_cycles = 0;
		bool stack_restored = false;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	Result Apply(Action action, float x, float y = 0.0f);
} // namespace AVPE::NativeCameraInput
