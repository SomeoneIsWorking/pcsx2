// AVPE guest-call execution boundary. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <array>

namespace AVPE::EECallShuttle
{
	enum class Status : u8
	{
		Success,
		InvalidRequest,
		WrongGame,
		VMUnavailable,
		UnsupportedCPU,
		Faulted,
		CycleBudgetExceeded,
		Interrupted,
	};

	struct Request
	{
		u32 function = 0;
		std::array<u64, 4> arguments{};
		u64 cycle_budget = 3'000'000;
	};

	struct Result
	{
		Status status = Status::Interrupted;
		u64 v0 = 0;
		u64 v1 = 0;
		u32 stopped_pc = 0;
		u64 elapsed_cycles = 0;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	// May be called from a non-CPU thread. The request is dispatched synchronously
	// to the VM thread and the interrupted EE architectural context is restored.
	Result Call(const Request& request);

	// A timed-out call may have partial guest-memory effects. Loading a known
	// state is the only operation which makes subsequent calls trustworthy.
	void ResetAfterStateLoad();
} // namespace AVPE::EECallShuttle
