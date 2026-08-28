// AVP:E native CInputData encoding. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <array>

namespace AVPE::NativeInputData
{
	std::array<u8, 8> EncodeFloatPair(float x, float y);
} // namespace AVPE::NativeInputData
