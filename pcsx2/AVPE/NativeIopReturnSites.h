// AVP:E bounded IOP oracle-return site registry. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

namespace AVPE::NativeIopReturnSites
{
	// Sites remain admitted for the process lifetime so IOP cache resets cannot
	// recompile a previously observed caller without its return hook.
	enum class Registration : u8
	{
		Existing,
		Added,
		Full,
	};

	Registration Register(u32 pc);
	bool Contains(u32 pc);
} // namespace AVPE::NativeIopReturnSites
