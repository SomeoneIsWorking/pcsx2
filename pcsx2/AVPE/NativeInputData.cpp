// AVP:E native CInputData encoding. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeInputData.h"

#include <bit>

namespace AVPE::NativeInputData
{
	std::array<u8, 8> EncodeFloatPair(const float x, const float y)
	{
		const u32 words[] = {std::bit_cast<u32>(x), std::bit_cast<u32>(y)};
		std::array<u8, 8> bytes{};
		for (u32 word_index = 0; word_index < 2; ++word_index)
		{
			for (u32 byte_index = 0; byte_index < 4; ++byte_index)
				bytes[word_index * 4 + byte_index] =
					static_cast<u8>(words[word_index] >> (byte_index * 8));
		}
		return bytes;
	}
} // namespace AVPE::NativeInputData
