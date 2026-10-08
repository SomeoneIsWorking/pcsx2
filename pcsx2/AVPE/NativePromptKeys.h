// PC keys for AVP:E's prompted menu items. Fork-local.

#pragma once

#include "AVPE/NativePromptGlyphs.h"

#include <bitset>
#include <string_view>

namespace AVPE
{
	// How a PC player triggers one prompted item, as in a PC RTS: Enter confirms,
	// Esc backs out, and every other command takes a letter of its label.
	struct PromptKey
	{
		enum class Kind : u8
		{
			Confirm,
			Back,
			Command,
		};

		Kind kind = Kind::Confirm;
		// 'A'..'Z' for a command.
		char letter = 0;
	};

	// Assigns the keys of one frame's prompts in draw order.
	class NativePromptKeys final
	{
	public:
		// GMenuItem::HotKey values, GetCRC of the input event names.
		static inline constexpr u32 FrontEndSelect = 0x39504A77;
		static inline constexpr u32 FrontEndBack = 0xC5AA0E7F;
		static inline constexpr u32 MenuTriangleRelease = 0x2E16A928;

		// An item with a hotkey is triggered by that event; one without is left to its
		// menu, which reacts to the drawn button, Cross confirming and Triangle backing out.
		static PromptKey::Kind KindFor(u32 hotkey, PromptButton button);

		// A command takes the first letter of its label that no earlier prompt in the
		// frame holds, else the first free letter of the alphabet.
		PromptKey Next(u32 hotkey, PromptButton button, std::string_view label);

	private:
		std::bitset<26> m_taken;
	};
} // namespace AVPE
