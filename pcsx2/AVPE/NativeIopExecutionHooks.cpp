// AVP:E IOP execution-hook composition. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeIopExecutionHooks.h"

#include "AVPE/NativeBiosTrace.h"
#include "R3000A.h"

namespace AVPE::NativeIopExecutionHooks
{
	void ObserveIopExecution(const u32 pc)
	{
		NativeBiosTrace::RecordIopOracleImportReturn(
			psxRegs.GPR.n.sp, pc, static_cast<s32>(psxRegs.GPR.n.v0));
	}
} // namespace AVPE::NativeIopExecutionHooks
