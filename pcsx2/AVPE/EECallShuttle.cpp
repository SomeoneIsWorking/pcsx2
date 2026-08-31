// AVPE guest-call execution boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/EECallShuttle.h"

#include "AVPE/NativeHostYield.h"

#include "Host.h"
#include "R5900.h"
#include "VMManager.h"
#include "VU.h"
#include "vtlb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iterator>
#include <optional>

namespace AVPE::EECallShuttle
{
	static constexpr const char* TARGET_SERIAL = "SLUS-20147";
	static constexpr u32 TARGET_CRC = 0x64DA78A3;
	static constexpr u32 TARGET_TEXT_BEGIN = 0x00100000;
	static constexpr u32 TARGET_TEXT_END = 0x00366D00;
	static constexpr u64 MAX_CYCLE_BUDGET = 30'000'000;
	static constexpr u32 GUEST_CALL_FRAME_SIZE = 0x40;
	static constexpr u32 GUEST_ARGUMENT_HOME_SIZE = 0x10;
	static constexpr u32 GUEST_STACK_BUFFER_SIZE = GUEST_CALL_FRAME_SIZE - GUEST_ARGUMENT_HOME_SIZE;
	static std::atomic_bool s_faulted{false};
	static std::atomic_bool s_active{false};
	static u32 s_last_avpe_text_pc = 0;

	struct DeferredCall
	{
		DeferredState state = DeferredState::Idle;
		u64 id = 0;
		u64 next_id = 1;
		cpuRegisters saved_cpu{};
		fpuRegisters saved_fpu{};
		VURegs saved_vu0{};
		std::array<u8, GUEST_CALL_FRAME_SIZE> saved_stack{};
		u32 stack_address = 0;
		u32 return_pc = 0;
		Result result{};
	};

	static DeferredCall s_deferred;

	class ActiveCallGuard final
	{
	public:
		ActiveCallGuard()
			: m_acquired(!s_active.exchange(true, std::memory_order_acq_rel))
		{
		}

		~ActiveCallGuard()
		{
			if (m_acquired)
				s_active.store(false, std::memory_order_release);
		}

		bool Acquired() const { return m_acquired; }

	private:
		bool m_acquired;
	};

	class GuestStackFrame final
	{
	public:
		bool Stage(const std::span<const u8> bytes)
		{
			if (bytes.empty() || bytes.size() > GUEST_STACK_BUFFER_SIZE)
				return false;

			const u32 interrupted_sp = cpuRegs.GPR.n.sp.UL[0];
			if ((interrupted_sp & 0xf) != 0 || interrupted_sp < GUEST_CALL_FRAME_SIZE)
				return false;

			m_address = interrupted_sp - GUEST_CALL_FRAME_SIZE;
			const u32 frame_end = m_address + GUEST_CALL_FRAME_SIZE - 1;
			if (frame_end < m_address)
				return false;

			// AVP:E's user stack is in the direct low EE main-RAM mapping. Do not
			// use vtlb_V2P here: its ppmap is optional and absent for this title.
			if (m_address >= Ps2MemSize::ExposedRam || frame_end >= Ps2MemSize::ExposedRam)
				return false;

			if (!vtlb_memSafeReadBytes(m_address, m_original.data(), m_original.size()))
				return false;
			m_staged = m_original;
			std::memcpy(m_staged.data() + GUEST_ARGUMENT_HOME_SIZE, bytes.data(), bytes.size());
			if (!vtlb_memSafeWriteBytes(m_address, m_staged.data(), m_staged.size()) ||
				vtlb_memSafeCmpBytes(m_address, m_staged.data(), m_staged.size()) != 0)
			{
				m_restore_failed = !RestoreBytes();
				return false;
			}

			cpuRegs.GPR.n.sp.UD[1] = 0;
			cpuRegs.GPR.n.sp.UL[0] = m_address;
			m_active = true;
			return true;
		}

		bool Restore()
		{
			if (!m_active)
				return !m_restore_failed;
			m_active = false;
			m_restore_failed = !RestoreBytes();
			return !m_restore_failed;
		}

		u32 DataAddress() const { return m_address == 0 ? 0 : m_address + GUEST_ARGUMENT_HOME_SIZE; }

	private:
		bool RestoreBytes()
		{
			return vtlb_memSafeWriteBytes(m_address, m_original.data(), m_original.size()) &&
			       vtlb_memSafeCmpBytes(m_address, m_original.data(), m_original.size()) == 0;
		}

		std::array<u8, GUEST_CALL_FRAME_SIZE> m_original{};
		std::array<u8, GUEST_CALL_FRAME_SIZE> m_staged{};
		u32 m_address = 0;
		bool m_active = false;
		bool m_restore_failed = false;
	};

	static Result Fail(const Status status, const char* error)
	{
		return {.status = status, .error = error};
	}

	static Result ValidateRequest(const Request& request)
	{
		if ((request.function & 3) != 0 || request.function < TARGET_TEXT_BEGIN ||
			request.function >= TARGET_TEXT_END)
		{
			return Fail(Status::InvalidRequest, "function must be an aligned address in the AVP:E executable text");
		}
		if (request.cycle_budget == 0 || request.cycle_budget > MAX_CYCLE_BUDGET)
			return Fail(Status::InvalidRequest, "cycle budget must be in 1..30000000");
		if (s_faulted.load(std::memory_order_acquire))
			return Fail(Status::Faulted, "EE call shuttle is faulted; load a known state before another call");
		if (!VMManager::HasValidVM())
			return Fail(Status::VMUnavailable, "no valid VM");
		if (VMManager::GetDiscSerial() != TARGET_SERIAL || VMManager::GetDiscCRC() != TARGET_CRC)
			return Fail(Status::WrongGame, "loaded executable is not the supported AVP:E revision");
		if (Cpu == nullptr || Cpu->ExecuteUntil == nullptr)
			return Fail(Status::UnsupportedCPU, "active EE engine cannot execute to a call boundary");
		return {.status = Status::Success};
	}

	static void PreserveAdvancedTiming(const cpuRegisters& completed)
	{
		cpuRegs.PERF = completed.PERF;
		std::copy(std::begin(completed.eCycle), std::end(completed.eCycle), std::begin(cpuRegs.eCycle));
		std::copy(std::begin(completed.sCycle), std::end(completed.sCycle), std::begin(cpuRegs.sCycle));
		cpuRegs.cycle = completed.cycle;
		cpuRegs.interrupt = completed.interrupt;
		cpuRegs.tempcycles = completed.tempcycles;
		cpuRegs.dmastall = completed.dmastall;
		cpuRegs.nextEventCycle = completed.nextEventCycle;
		cpuRegs.lastEventCycle = completed.lastEventCycle;
		cpuRegs.lastCOP0Cycle = completed.lastCOP0Cycle;
		cpuRegs.lastPERFCycle[0] = completed.lastPERFCycle[0];
		cpuRegs.lastPERFCycle[1] = completed.lastPERFCycle[1];
		cpuRegs.CP0.n.Random = completed.CP0.n.Random;
		cpuRegs.CP0.n.Count = completed.CP0.n.Count;
		cpuRegs.CP0.n.Cause = completed.CP0.n.Cause;
	}

	static Result CallOnCPUThread(
		const Request& request, const std::optional<u32> stack_argument, const std::span<const u8> stack_bytes)
	{
		const Result validation = ValidateRequest(request);
		if (!validation.Succeeded())
			return validation;
		if (s_deferred.state == DeferredState::Running)
			return Fail(Status::Busy, "a deferred EE call is still running");
		if (stack_argument.has_value() &&
			(*stack_argument >= request.arguments.size() || stack_bytes.empty() ||
				stack_bytes.size() > GUEST_STACK_BUFFER_SIZE))
		{
			return Fail(Status::InvalidRequest,
				"guest stack argument must name a0..a3 and contain at most 48 bytes");
		}
		if (!stack_argument.has_value() && !stack_bytes.empty())
			return Fail(Status::InvalidRequest, "guest stack bytes require an argument index");

		ActiveCallGuard active_call;
		if (!active_call.Acquired())
			return Fail(Status::InvalidRequest, "nested EE calls are not supported");

		const cpuRegisters saved_cpu = cpuRegs;
		const fpuRegisters saved_fpu = fpuRegs;
		const VURegs saved_vu0 = VU0;
		const u32 return_pc = saved_cpu.pc;
		GuestStackFrame stack_frame;
		if (stack_argument.has_value() && !stack_frame.Stage(stack_bytes))
		{
			const bool restored = stack_frame.Restore();
			if (!restored)
				s_faulted.store(true, std::memory_order_release);
			return {
				.status = Status::GuestMemoryError,
				.staging_address = stack_frame.DataAddress(),
				.stack_restored = restored,
				.error = restored ? "guest stack frame is not writable main RAM" :
			                        "guest stack staging failed and exact restoration could not be verified",
			};
		}

		for (u32 i = 0; i < request.arguments.size(); ++i)
		{
			cpuRegs.GPR.r[4 + i].UD[1] = 0;
			cpuRegs.GPR.r[4 + i].UD[0] = request.arguments[i];
		}
		if (stack_argument.has_value())
			cpuRegs.GPR.r[4 + *stack_argument].UD[0] = stack_frame.DataAddress();
		cpuRegs.GPR.n.ra.UD[1] = 0;
		cpuRegs.GPR.n.ra.UL[0] = return_pc;
		cpuRegs.pc = request.function;
		cpuRegs.code = 0;
		cpuRegs.IsDelaySlot = 0;
		cpuRegs.branch = 0;
		cpuRegs.pcWriteback = 0;

		s_last_avpe_text_pc = 0;
		const EEExecutionResult execution = Cpu->ExecuteUntil(return_pc, request.cycle_budget);
		const cpuRegisters completed_cpu = cpuRegs;
		const u64 v0 = completed_cpu.GPR.n.v0.UD[0];
		const u64 v1 = completed_cpu.GPR.n.v1.UD[0];
		const u32 stopped_pc = completed_cpu.pc;
		const u64 elapsed_cycles = completed_cpu.cycle - saved_cpu.cycle;

		cpuRegs = saved_cpu;
		fpuRegs = saved_fpu;
		VU0 = saved_vu0;
		PreserveAdvancedTiming(completed_cpu);
		const bool stack_restored = stack_frame.Restore();

		Result result = {
			.v0 = v0,
			.v1 = v1,
			.return_pc = return_pc,
			.stopped_pc = stopped_pc,
			.last_avpe_text_pc = s_last_avpe_text_pc,
			.staging_address = stack_argument.has_value() ? stack_frame.DataAddress() : 0,
			.elapsed_cycles = elapsed_cycles,
			.stack_restored = stack_restored,
		};
		if (!stack_restored)
		{
			result.status = Status::GuestMemoryError;
			result.error = "guest call returned but exact stack restoration could not be verified";
			s_faulted.store(true, std::memory_order_release);
			return result;
		}
		switch (execution)
		{
			case EEExecutionResult::ReachedTarget:
				result.status = Status::Success;
				return result;
			case EEExecutionResult::CycleBudgetExceeded:
				result.status = Status::CycleBudgetExceeded;
				result.error = "guest call exceeded its EE cycle budget; reload a known state before continuing";
				s_faulted.store(true, std::memory_order_release);
				return result;
			case EEExecutionResult::Interrupted:
				result.status = Status::Interrupted;
				result.error = "guest execution was interrupted; reload a known state before continuing";
				s_faulted.store(true, std::memory_order_release);
				return result;
		}
		s_faulted.store(true, std::memory_order_release);
		return Fail(Status::Interrupted, "unknown EE execution result");
	}

	static DeferredTicket QueueDeferredOnCPUThread(const Request& request)
	{
		const Result validation = ValidateRequest(request);
		if (!validation.Succeeded())
			return {.status = validation.status, .error = validation.error};
		if (VMManager::GetState() != VMState::Running)
			return {.status = Status::VMUnavailable, .error = "VM must be running for a deferred EE call"};
		if (s_active.load(std::memory_order_acquire) || s_deferred.state == DeferredState::Running)
			return {.status = Status::Busy, .error = "another EE call is still running"};

		const u32 interrupted_sp = cpuRegs.GPR.n.sp.UL[0];
		if ((interrupted_sp & 0xf) != 0 || interrupted_sp < GUEST_CALL_FRAME_SIZE)
			return {.status = Status::GuestMemoryError, .error = "interrupted guest stack is not aligned"};
		const u32 stack_address = interrupted_sp - GUEST_CALL_FRAME_SIZE;
		if (stack_address >= Ps2MemSize::ExposedRam ||
			stack_address + GUEST_CALL_FRAME_SIZE > Ps2MemSize::ExposedRam ||
			!vtlb_memSafeReadBytes(stack_address, s_deferred.saved_stack.data(), s_deferred.saved_stack.size()))
		{
			return {.status = Status::GuestMemoryError, .error = "guest call frame is not readable main RAM"};
		}

		s_deferred.saved_cpu = cpuRegs;
		s_deferred.saved_fpu = fpuRegs;
		s_deferred.saved_vu0 = VU0;
		s_deferred.stack_address = stack_address;
		s_deferred.return_pc = cpuRegs.pc;
		s_deferred.result = {};
		s_deferred.id = s_deferred.next_id++;
		if (s_deferred.next_id == 0)
			s_deferred.next_id = 1;

		for (u32 i = 0; i < request.arguments.size(); ++i)
		{
			cpuRegs.GPR.r[4 + i].UD[1] = 0;
			cpuRegs.GPR.r[4 + i].UD[0] = request.arguments[i];
		}
		cpuRegs.GPR.n.sp.UD[1] = 0;
		cpuRegs.GPR.n.sp.UL[0] = stack_address;
		cpuRegs.GPR.n.ra.UD[1] = 0;
		cpuRegs.GPR.n.ra.UL[0] = s_deferred.return_pc;
		cpuRegs.pc = request.function;
		cpuRegs.code = 0;
		cpuRegs.IsDelaySlot = 0;
		cpuRegs.branch = 0;
		cpuRegs.pcWriteback = 0;
		s_deferred.state = DeferredState::Running;

		// The return address may already have a linked recompiler block. Clearing
		// its first word forces the dispatch through TryCompleteDeferredCall before
		// the interrupted instruction is allowed to execute. The queue operation
		// runs from a host event callback, so request a block-end exit and return
		// through that callback instead of fast-jumping across its stack frames.
		Cpu->Clear(s_deferred.return_pc, 1);
		Cpu->ExitExecution();
		return {.status = Status::Success, .id = s_deferred.id};
	}

	bool TryCompleteDeferredCall(const u32 pc)
	{
		if (s_deferred.state != DeferredState::Running || pc != s_deferred.return_pc)
			return false;

		const cpuRegisters completed_cpu = cpuRegs;
		const bool stack_restored =
			vtlb_memSafeWriteBytes(s_deferred.stack_address, s_deferred.saved_stack.data(),
				s_deferred.saved_stack.size()) &&
			vtlb_memSafeCmpBytes(s_deferred.stack_address, s_deferred.saved_stack.data(),
				s_deferred.saved_stack.size()) == 0;
		const u64 v0 = completed_cpu.GPR.n.v0.UD[0];
		const u64 v1 = completed_cpu.GPR.n.v1.UD[0];
		const u64 elapsed_cycles = completed_cpu.cycle - s_deferred.saved_cpu.cycle;

		cpuRegs = s_deferred.saved_cpu;
		fpuRegs = s_deferred.saved_fpu;
		VU0 = s_deferred.saved_vu0;
		PreserveAdvancedTiming(completed_cpu);

		s_deferred.result = {
			.status = stack_restored ? Status::Success : Status::GuestMemoryError,
			.v0 = v0,
			.v1 = v1,
			.stopped_pc = pc,
			.staging_address = s_deferred.stack_address,
			.elapsed_cycles = elapsed_cycles,
			.stack_restored = stack_restored,
			.error = stack_restored ? "" : "deferred guest call returned but exact stack restoration failed",
		};
		s_deferred.state = stack_restored ? DeferredState::Completed : DeferredState::Failed;
		if (!stack_restored)
			s_faulted.store(true, std::memory_order_release);
		return true;
	}

	void ObserveExecuteUntilPc(const u32 pc)
	{
		if (pc >= TARGET_TEXT_BEGIN && pc < TARGET_TEXT_END)
			s_last_avpe_text_pc = pc;
	}

	Result Transaction::Call(const Request& request)
	{
		return CallOnCPUThread(request, std::nullopt, {});
	}

	Result Transaction::CallWithStackBuffer(
		const Request& request, const u32 argument_index, const std::span<const u8> bytes)
	{
		return CallOnCPUThread(request, argument_index, bytes);
	}

	DeferredTicket Transaction::QueueDeferred(const Request& request)
	{
		return QueueDeferredOnCPUThread(request);
	}

	void RunTransaction(const std::function<void(Transaction&)>& operation)
	{
		NativeHostYield::Request();
		Host::RunOnCPUThread([&operation]() {
			Transaction transaction;
			operation(transaction);
			NativeHostYield::CompleteRequest();
		},
			true);
	}

	Result Call(const Request& request)
	{
		Result result;
		RunTransaction([&result, &request](Transaction& transaction) { result = transaction.Call(request); });
		return result;
	}

	DeferredSnapshot GetDeferredSnapshot()
	{
		DeferredSnapshot snapshot;
		RunTransaction([&snapshot](Transaction&) {
			snapshot.state = s_deferred.state;
			snapshot.id = s_deferred.id;
			snapshot.result = s_deferred.result;
		});
		return snapshot;
	}

	DeferredTicket QueueDeferredFromExecutionHook(const Request& request)
	{
		return QueueDeferredOnCPUThread(request);
	}

	void ResetAfterStateLoad()
	{
		s_faulted.store(false, std::memory_order_release);
		s_deferred.state = DeferredState::Idle;
		s_deferred.id = 0;
		s_deferred.result = {};
	}
} // namespace AVPE::EECallShuttle
