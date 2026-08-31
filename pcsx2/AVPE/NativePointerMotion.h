// Shared AVP:E absolute-pointer motion. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/EECallShuttle.h"

namespace AVPE::NativePointerMotion
{
	enum class Status : u8
	{
		Success,
		InvalidCoordinates,
		PointerUnavailable,
		ResolutionUnavailable,
		GuestMemoryError,
		ShuttleFailure,
	};

	struct Result
	{
		Status status = Status::ShuttleFailure;
		EECallShuttle::Status shuttle_status = EECallShuttle::Status::Interrupted;
		float screen_x = 0.0f;
		float screen_y = 0.0f;
		float observed_x = 0.0f;
		float observed_y = 0.0f;
		u32 pointer = 0;
		u32 staging_address = 0;
		u32 return_pc = 0;
		u32 stopped_pc = 0;
		u32 last_avpe_text_pc = 0;
		u64 elapsed_cycles = 0;
		bool stack_restored = false;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	struct RelativeInput
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	bool CoordinatesAreValid(float normalized_x, float normalized_y);
	bool ReadPosition(u32 pointer, float* x, float* y);
	bool ReadPhysicalPosition(u32 pointer, float* x, float* y);
	Result PrepareGAvPPointerRelativeInput(EECallShuttle::Transaction& transaction, u32 pointer,
		float normalized_x, float normalized_y, RelativeInput* input);
	Result MoveAbsolute(EECallShuttle::Transaction& transaction, u32 pointer,
		float normalized_x, float normalized_y);
} // namespace AVPE::NativePointerMotion
