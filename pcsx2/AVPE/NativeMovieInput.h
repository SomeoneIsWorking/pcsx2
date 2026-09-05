// AVP:E movie cancellation ownership. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/EECallShuttle.h"
#include "AVPE/GuestObjects.h"

#include <functional>

namespace AVPE::NativeMovieInput
{
	inline constexpr u32 AbortFunction = 0x00183C20;

	enum class State : u8
	{
		Idle,
		Pending,
		Dispatched,
		PlayerExited,
		Expired,
		Failed
	};
	enum class Admission : u8
	{
		Absent,
		Accepted,
		Rejected
	};

	struct Snapshot
	{
		State state = State::Idle;
		u64 id = 0;
		u32 player = 0;
		u32 decoder = 0;
		u64 deferred_call_id = 0;
		const char* error = "";
	};

	struct Result
	{
		Admission admission = Admission::Absent;
		Snapshot action;
		const char* error = "";
	};

	// CPU-thread-owned, one-shot input policy. Tests inject memory and dispatch
	// at these same production boundaries; no guest state is written here.
	class Cancellation final
	{
	public:
		using ReadWord = std::function<bool(u32, u32*)>;
		using QueueCall = std::function<EECallShuttle::DeferredTicket(const EECallShuttle::Request&)>;

		explicit Cancellation(ReadWord read = GuestObjects::ReadWord);
		Result Request();
		void ObserveLoop(u32 decoder);
		void EndPlayback(bool cancelled_queued_call = false);
		void EndPlayer(bool cancelled_queued_call = false);
		void Service(const QueueCall& queue);
		void Reset();
		const Snapshot& Inspect() const { return m_action; }

	private:
		enum class Phase : u8
		{
			Unknown,
			Playing,
			Finishing
		};
		bool ReadPlayer(u32* player, u32* skip_disabled) const;
		enum class Readiness : u8
		{
			Waiting,
			Ready,
			Ended,
			Invalid
		};
		struct AdmissionCheck
		{
			Readiness readiness = Readiness::Waiting;
			u32 decoder = 0;
			const char* error = "";
		};
		AdmissionCheck CheckAdmission() const;
		void Expire(bool cancelled_queued_call = false);
		void Fail(const char* error);

		ReadWord m_read;
		Snapshot m_action;
		u64 m_next_id = 1;
		Phase m_phase = Phase::Unknown;
		u32 m_observed_decoder = 0;
	};

	Result RequestOnCPUThread();
	void PollOnCPUThread();
	bool ShouldInstrumentEePc(u32 pc);
	void ObserveEeExecution(u32 pc);
	void Reset();
	Snapshot Inspect();
	const char* StateName(State state);
} // namespace AVPE::NativeMovieInput
