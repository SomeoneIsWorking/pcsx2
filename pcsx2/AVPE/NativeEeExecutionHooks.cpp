// AVP:E EE execution-hook composition. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeEeExecutionHooks.h"

#include "AVPE/GuestObjects.h"
#include "AVPE/NativeBiosTrace.h"
#include "AVPE/NativeGameLoadBoundary.h"
#include "AVPE/NativeGameSaveBoundary.h"
#include "AVPE/NativeHostYield.h"
#include "AVPE/NativeInputDispatch.h"
#include "AVPE/NativeMenuInput.h"
#include "AVPE/NativeMovieInput.h"
#include "AVPE/NativeMissionLoadTiming.h"
#include "AVPE/NativeShellShutdownBoundary.h"
#include "AVPE/NativeTitleTransition.h"
#include "R5900.h"

namespace AVPE::NativeEeExecutionHooks
{
	namespace
	{
		void ObserveBiosTrace(const u32 pc)
		{
			u32 stack_remaining = 0;
			const bool stack_remaining_valid =
				GuestObjects::ReadWord(cpuRegs.GPR.n.sp.UL[0] + 0x7C, &stack_remaining);
			NativeBiosTrace::ObserveMissionLoadProgress(
				pc, cpuRegs.GPR.n.a2.UL[0], cpuRegs.GPR.n.v0.UL[0], stack_remaining, stack_remaining_valid);
			NativeBiosTrace::ObserveMissionPostReadProgress(pc, cpuRegs.GPR.n.s2.UL[0]);
			u32 initializer_symbol = 0;
			u32 initializer_metadata = 0;
			const bool initializer_descriptor_valid =
				pc == 0x0017467C && GuestObjects::ReadWord(cpuRegs.GPR.n.s1.UL[0], &initializer_symbol) &&
				GuestObjects::ReadWord(cpuRegs.GPR.n.s1.UL[0] + 4, &initializer_metadata);
			NativeBiosTrace::ObserveMissionTypeInitializer(pc, cpuRegs.GPR.n.v0.UL[0], cpuRegs.GPR.n.s0.UL[0],
				cpuRegs.GPR.n.s1.UL[0], cpuRegs.GPR.n.s2.UL[0], cpuRegs.GPR.n.sp.UL[0], initializer_symbol,
				initializer_metadata, initializer_descriptor_valid);
			NativeBiosTrace::ObserveMissionObjectFactory(pc, cpuRegs.GPR.n.v0.UL[0], cpuRegs.GPR.n.a2.UL[0],
				cpuRegs.GPR.n.a1.UL[0], cpuRegs.GPR.n.a3.UL[0], cpuRegs.GPR.n.sp.UL[0]);
			NativeBiosTrace::ObserveMissionLoadError(pc, cpuRegs.GPR.n.a0.UL[0], cpuRegs.GPR.n.ra.UL[0]);
			NativeBiosTrace::ObserveMissionBoundary(pc);
		}
	} // namespace

	bool ShouldInstrumentEePc(const u32 pc)
	{
		return NativeBiosTrace::ShouldInstrumentEeSyscallReturn(pc) ||
		       NativeBiosTrace::ShouldInstrumentMissionBoundary(pc) ||
		       NativeGameLoadBoundary::ShouldInstrumentEePc(pc) ||
		       NativeGameSaveBoundary::ShouldInstrumentEePc(pc) ||
		       NativeShellShutdownBoundary::ShouldInstrumentEePc(pc) ||
		       NativeTitleTransition::ShouldInstrumentEePc(pc) ||
		       NativeMissionLoadTiming::ShouldInstrumentEePc(pc) || NativeHostYield::ShouldInstrumentEePc(pc) ||
		       NativeInputDispatch::ShouldInstrumentEePc(pc) || NativeMenuInput::ShouldObserveEePc(pc) ||
		       NativeMovieInput::ShouldInstrumentEePc(pc);
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (NativeBiosTrace::ShouldInstrumentEeSyscallReturn(pc))
			NativeBiosTrace::ObserveEeSyscallReturn(pc);
		if (NativeBiosTrace::ShouldInstrumentMissionBoundary(pc))
			ObserveBiosTrace(pc);
		if (NativeGameLoadBoundary::ShouldInstrumentEePc(pc))
			NativeGameLoadBoundary::ObserveEeExecution(pc);
		if (NativeGameSaveBoundary::ShouldInstrumentEePc(pc))
			NativeGameSaveBoundary::ObserveEeExecution(pc);
		if (NativeShellShutdownBoundary::ShouldInstrumentEePc(pc))
			NativeShellShutdownBoundary::ObserveEeExecution(pc);
		if (NativeTitleTransition::ShouldInstrumentEePc(pc))
			NativeTitleTransition::ObserveEeExecution(pc);
		if (NativeMissionLoadTiming::ShouldInstrumentEePc(pc))
			NativeMissionLoadTiming::ObserveEeExecution(pc);
		if (NativeHostYield::ShouldInstrumentEePc(pc))
			NativeHostYield::ObserveEeExecution(pc);
		if (NativeInputDispatch::ShouldInstrumentEePc(pc))
			NativeInputDispatch::ObserveEeExecution(pc);
		if (NativeMenuInput::ShouldObserveEePc(pc))
			NativeMenuInput::ObserveInputProcess();
		if (NativeMovieInput::ShouldInstrumentEePc(pc))
			NativeMovieInput::ObserveEeExecution(pc);
	}
} // namespace AVPE::NativeEeExecutionHooks
