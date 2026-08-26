// AVPE guest-call execution boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/EECallShuttle.h"

#include "Host.h"
#include "R5900.h"
#include "VMManager.h"
#include "VU.h"

#include <algorithm>
#include <atomic>
#include <iterator>

namespace AVPE::EECallShuttle
{
	static constexpr const char* TARGET_SERIAL = "SLUS-20147";
	static constexpr u32 TARGET_CRC = 0x64DA78A3;
	static constexpr u32 TARGET_TEXT_BEGIN = 0x00100000;
	static constexpr u32 TARGET_TEXT_END = 0x00366D00;
	static constexpr u64 MAX_CYCLE_BUDGET = 30'000'000;
	static std::atomic_bool s_faulted{false};

	static Result Fail(const Status status, const char* error)
	{
		return {.status = status, .error = error};
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

	static Result CallOnCPUThread(const Request& request)
	{
		if (!VMManager::HasValidVM())
			return Fail(Status::VMUnavailable, "no valid VM");
		if (VMManager::GetDiscSerial() != TARGET_SERIAL || VMManager::GetDiscCRC() != TARGET_CRC)
			return Fail(Status::WrongGame, "loaded executable is not the supported AVP:E revision");
		if (Cpu == nullptr || Cpu->ExecuteUntil == nullptr)
			return Fail(Status::UnsupportedCPU, "active EE engine cannot execute to a call boundary");

		const cpuRegisters saved_cpu = cpuRegs;
		const fpuRegisters saved_fpu = fpuRegs;
		const VURegs saved_vu0 = VU0;
		const u32 return_pc = saved_cpu.pc;

		for (u32 i = 0; i < request.arguments.size(); ++i)
		{
			cpuRegs.GPR.r[4 + i].UD[1] = 0;
			cpuRegs.GPR.r[4 + i].UD[0] = request.arguments[i];
		}
		cpuRegs.GPR.n.ra.UD[1] = 0;
		cpuRegs.GPR.n.ra.UL[0] = return_pc;
		cpuRegs.pc = request.function;
		cpuRegs.code = 0;
		cpuRegs.IsDelaySlot = 0;
		cpuRegs.branch = 0;
		cpuRegs.pcWriteback = 0;

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

		Result result = {
			.v0 = v0,
			.v1 = v1,
			.stopped_pc = stopped_pc,
			.elapsed_cycles = elapsed_cycles,
		};
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
				result.error = "guest execution was interrupted before the function returned";
				return result;
		}
		return Fail(Status::Interrupted, "unknown EE execution result");
	}

	Result Call(const Request& request)
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

		Result result;
		Host::RunOnCPUThread([&result, &request]() { result = CallOnCPUThread(request); }, true);
		return result;
	}

	void ResetAfterStateLoad()
	{
		s_faulted.store(false, std::memory_order_release);
	}
} // namespace AVPE::EECallShuttle
