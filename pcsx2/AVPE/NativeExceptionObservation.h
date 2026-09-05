// AVP:E CPU exception-transition observation. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE
{
	// CPU-thread scope only. Reads the actual exception routine's before/after
	// registers on every return path; never predicts or alters their values.
	class NativeExceptionObservation final
	{
	public:
		enum class Domain : u8
		{
			Ee,
			Iop
		};
		NativeExceptionObservation(Domain domain, u32 code, bool branch_delay);
		~NativeExceptionObservation();
		NativeExceptionObservation(const NativeExceptionObservation&) = delete;
		NativeExceptionObservation& operator=(const NativeExceptionObservation&) = delete;

	private:
		Domain m_domain;
		u32 m_code;
		u32 m_pc;
		u32 m_status;
		bool m_branch_delay;
		bool m_enabled;
	};
} // namespace AVPE
