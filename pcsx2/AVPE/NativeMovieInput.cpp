// AVP:E movie cancellation ownership. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMovieInput.h"

#include "R5900.h"
#include "VMManager.h"

#include <utility>

namespace AVPE::NativeMovieInput
{
	namespace
	{
		constexpr u32 PlayerSingleton = 0x003672BC;
		constexpr u32 PlayerVtable = 0x00334420;
		constexpr u32 DecoderSingleton = 0x0036741C;
		constexpr u32 MovieLoopPc = 0x00180DC0;
		constexpr u32 MovieFlushPc = 0x00180F40;
		constexpr u32 PlayerConstructorPc = 0x0016B6A0;
		constexpr u32 PlayerDestructorPc = 0x0016B950;
		Cancellation s_cancellation;

		bool IsSupportedTarget()
		{
			return VMManager::GetDiscSerial() == "SLUS-20147" && VMManager::GetDiscCRC() == 0x64DA78A3;
		}
	} // namespace

	Cancellation::Cancellation(ReadWord read)
		: m_read(std::move(read))
	{
	}

	bool Cancellation::ReadPlayer(u32* player, u32* skip_disabled) const
	{
		if (!m_read(PlayerSingleton, player))
			return false;
		if (*player == 0)
			return true;
		u32 vtable = 0;
		return GuestObjects::IsPlausibleAddress(*player) && m_read(*player, &vtable) &&
		       vtable == PlayerVtable && m_read(*player + 0x3C, skip_disabled);
	}

	Result Cancellation::Request()
	{
		u32 player = 0;
		u32 skip_disabled = 0;
		if (!ReadPlayer(&player, &skip_disabled))
			return {.admission = Admission::Rejected, .error = "movie player identity is unreadable or invalid"};
		if (player == 0)
			return {};
		if (skip_disabled != 0 || m_phase == Phase::Finishing)
			return {.admission = Admission::Rejected, .error = "movie does not currently admit cancellation"};
		if (m_action.state == State::Pending ||
			(m_action.state == State::Dispatched && m_action.player == player))
			return {.admission = Admission::Rejected, .error = "movie cancellation already accepted"};
		m_action = {.state = State::Pending, .id = m_next_id++, .player = player};
		return {.admission = Admission::Accepted, .action = m_action};
	}

	void Cancellation::ObserveLoop(const u32 decoder)
	{
		m_phase = Phase::Playing;
		m_observed_decoder = decoder;
	}

	void Cancellation::Expire(const bool cancelled_queued_call)
	{
		if (m_action.state == State::Pending || (cancelled_queued_call && m_action.state == State::Dispatched))
		{
			m_action.state = State::Expired;
			m_action.error = "movie ended before cancellation dispatch";
		}
	}

	void Cancellation::EndPlayback(const bool cancelled_queued_call)
	{
		m_phase = Phase::Finishing;
		m_observed_decoder = 0;
		Expire(cancelled_queued_call);
	}

	void Cancellation::EndPlayer(const bool cancelled_queued_call)
	{
		m_phase = Phase::Unknown;
		m_observed_decoder = 0;
		Expire(cancelled_queued_call);
		if (m_action.state == State::Dispatched)
			m_action.state = State::PlayerExited;
	}

	void Cancellation::Fail(const char* error)
	{
		m_action.state = State::Failed;
		m_action.error = error;
	}

	Cancellation::AdmissionCheck Cancellation::CheckAdmission() const
	{
		u32 player = 0;
		u32 skip_disabled = 0;
		if (!ReadPlayer(&player, &skip_disabled))
			return {.readiness = Readiness::Invalid, .error = "pending movie player identity is unreadable or invalid"};
		if (player != m_action.player || skip_disabled != 0)
			return {.readiness = Readiness::Ended};
		if (m_phase != Phase::Playing)
			return {};
		u32 decoder = 0;
		u32 frames = 0;
		u32 state = 0;
		if (!m_read(DecoderSingleton, &decoder) || decoder != m_observed_decoder ||
			(m_action.decoder != 0 && decoder != m_action.decoder) ||
			!GuestObjects::IsPlausibleAddress(decoder) || !m_read(decoder + 8, &frames) ||
			!m_read(decoder + 0xA8, &state))
			return {.readiness = Readiness::Invalid, .error = "pending movie decoder does not match the live MPEG loop"};
		if (state != 0)
			return {.readiness = Readiness::Ended};
		// readMpeg's own physical-input path admits abort only after eleven
		// decoded frames. Preserve early native input without advancing time.
		if (static_cast<s32>(frames) < 11)
			return {};
		return {.readiness = Readiness::Ready, .decoder = decoder};
	}

	void Cancellation::Service(const QueueCall& queue)
	{
		if (m_action.state != State::Pending)
			return;
		const AdmissionCheck check = CheckAdmission();
		switch (check.readiness)
		{
			case Readiness::Waiting:
				return;
			case Readiness::Ended:
				Expire();
				return;
			case Readiness::Invalid:
				Fail(check.error);
				return;
			case Readiness::Ready:
				break;
		}
		m_action.decoder = check.decoder;
		const auto ticket = queue({.function = AbortFunction, .arguments = {check.decoder, 0, 0, 0}, .caller_begin = 0x00180D70, .caller_end = 0x00180F40, .can_execute = [this, id = m_action.id]() {
									   return m_action.id == id && m_action.state == State::Dispatched &&
			                                  CheckAdmission().readiness == Readiness::Ready;
								   }});
		if (!ticket.Accepted())
		{
			Fail(ticket.error);
			return;
		}
		m_action.state = State::Dispatched;
		m_action.deferred_call_id = ticket.id;
	}

	void Cancellation::Reset()
	{
		m_action = {};
		m_phase = Phase::Unknown;
		m_observed_decoder = 0;
	}

	Result RequestOnCPUThread()
	{
		return IsSupportedTarget() ? s_cancellation.Request() : Result{};
	}

	void PollOnCPUThread()
	{
		if (s_cancellation.Inspect().state != State::Pending || !IsSupportedTarget())
			return;
		// This is a host event boundary, never an EE instruction observer.
		EECallShuttle::RunTransaction([](EECallShuttle::Transaction& transaction) {
			s_cancellation.Service([&transaction](const EECallShuttle::Request& request) {
				return transaction.QueueDeferred(request);
			});
		});
	}

	bool ShouldInstrumentEePc(const u32 pc)
	{
		return pc == MovieLoopPc || pc == MovieFlushPc ||
		       pc == PlayerConstructorPc || pc == PlayerDestructorPc;
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (!IsSupportedTarget())
			return;
		if (pc == MovieLoopPc)
			s_cancellation.ObserveLoop(cpuRegs.GPR.n.s2.UL[0]);
		else if (pc == MovieFlushPc)
			s_cancellation.EndPlayback(EECallShuttle::CancelQueuedCall(s_cancellation.Inspect().deferred_call_id));
		else if (pc == PlayerConstructorPc || pc == PlayerDestructorPc)
			s_cancellation.EndPlayer(EECallShuttle::CancelQueuedCall(s_cancellation.Inspect().deferred_call_id));
	}

	void Reset()
	{
		s_cancellation.Reset();
	}

	Snapshot Inspect()
	{
		Snapshot snapshot;
		EECallShuttle::RunTransaction([&snapshot](EECallShuttle::Transaction&) {
			snapshot = s_cancellation.Inspect();
		});
		return snapshot;
	}

	const char* StateName(const State state)
	{
		switch (state)
		{
			case State::Pending:
				return "pending";
			case State::Dispatched:
				return "dispatched";
			case State::PlayerExited:
				return "player-exited";
			case State::Expired:
				return "expired";
			case State::Failed:
				return "failed";
			case State::Idle:
				return "idle";
		}
		return "invalid";
	}
} // namespace AVPE::NativeMovieInput
