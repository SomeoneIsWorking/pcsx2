// AVPE guest-call execution boundary. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <array>
#include <functional>
#include <span>

namespace AVPE::EECallShuttle
{
	enum class Status : u8
	{
		Success,
		InvalidRequest,
		WrongGame,
		VMUnavailable,
		UnsupportedCPU,
		Busy,
		Faulted,
		GuestMemoryError,
		CycleBudgetExceeded,
		Interrupted,
	};

	enum class DeferredState : u8
	{
		Idle,
		Running,
		Completed,
		Failed,
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
		u32 return_pc = 0;
		u32 stopped_pc = 0;
		u32 last_avpe_text_pc = 0;
		u32 staging_address = 0;
		u64 elapsed_cycles = 0;
		bool stack_restored = true;
		const char* error = "";

		bool Succeeded() const { return status == Status::Success; }
	};

	struct DeferredTicket
	{
		Status status = Status::Interrupted;
		u64 id = 0;
		const char* error = "";

		bool Accepted() const { return status == Status::Success; }
	};

	struct DeferredSnapshot
	{
		DeferredState state = DeferredState::Idle;
		u64 id = 0;
		Result result;
	};

	// May be called from a non-CPU thread. The request is dispatched synchronously
	// to the VM thread and the interrupted EE architectural context is restored.
	Result Call(const Request& request);

	class Transaction final
	{
	public:
		Result Call(const Request& request);
		Result CallWithStackBuffer(const Request& request, u32 argument_index, std::span<const u8> bytes);
		DeferredTicket QueueDeferred(const Request& request);

	private:
		Transaction() = default;
		friend void RunTransaction(const std::function<void(Transaction&)>& operation);
	};

	// Runs one cohesive operation under a single blocking CPU-thread dispatch.
	// Transaction is the only CPU-thread execution token exposed to AVPE owners.
	void RunTransaction(const std::function<void(Transaction&)>& operation);
	DeferredSnapshot GetDeferredSnapshot();

	// CPU-core boundary hook. A deferred call runs through the ordinary VM
	// scheduler, then this restores the interrupted architectural context before
	// the original return PC executes.
	bool TryCompleteDeferredCall(u32 pc);

	// Interpreter-only observation for the currently active bounded call. This
	// retains the final AVP:E text PC even when guest scheduling later enters
	// the EE BIOS idle thread.
	void ObserveExecuteUntilPc(u32 pc);

	// A timed-out call may have partial guest-memory effects. Loading a known
	// state is the only operation which makes subsequent calls trustworthy.
	void ResetAfterStateLoad();
} // namespace AVPE::EECallShuttle
