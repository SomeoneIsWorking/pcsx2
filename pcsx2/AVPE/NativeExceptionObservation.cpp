// AVP:E CPU exception-transition observation. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeExceptionObservation.h"
#include "AVPE/NativeBiosTrace.h"
#include "R5900.h"
#include "R3000A.h"

namespace AVPE
{
	NativeExceptionObservation::NativeExceptionObservation(const Domain domain, const u32 code, const bool branch_delay)
		: m_domain(domain)
		, m_code(code)
		, m_pc(domain == Domain::Ee ? cpuRegs.pc : psxRegs.pc)
		, m_status(domain == Domain::Ee ? cpuRegs.CP0.n.Status.val : psxRegs.CP0.n.Status)
		, m_branch_delay(branch_delay)
		, m_enabled(NativeBiosTrace::IsEnabled())
	{
	}

	NativeExceptionObservation::~NativeExceptionObservation()
	{
		if (!m_enabled)
			return;
		const NativeBiosTrace::ExceptionTransition transition = m_domain == Domain::Ee ?
		                                                            NativeBiosTrace::ExceptionTransition{m_status, cpuRegs.CP0.n.Status.val, cpuRegs.CP0.n.Cause, cpuRegs.CP0.n.EPC, cpuRegs.pc} :
		                                                            NativeBiosTrace::ExceptionTransition{m_status, psxRegs.CP0.n.Status, psxRegs.CP0.n.Cause, psxRegs.CP0.n.EPC, psxRegs.pc};
		NativeBiosTrace::RecordException(m_domain == Domain::Ee ? "ee" : "iop", m_code, m_pc, m_branch_delay, transition);
	}
} // namespace AVPE
