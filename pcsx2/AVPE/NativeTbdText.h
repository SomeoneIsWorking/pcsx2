// PC wording for AVP:E's PS2-specific TBD strings. Fork-local.

#pragma once

#include "AVPE/NativeMenuInput.h"

#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace AVPE
{
	// Rewrites published TBD strings in place as each file loads, before its objects
	// read them. Only text still exactly the original is touched, never grown.
	class NativeTbdText final
	{
	public:
		// CTbdFile::SetupPublics epilogue; InitTypes and object creation follow.
		static inline constexpr u32 SetupPublicsExit = 0x00174520;
		// CTbdFixupManager::pSymbolTable: {mask, buckets}; a bucket is
		// {entries, last index, capacity}; an entry is {value, crc, label}.
		static inline constexpr u32 SymbolTablePointer = 0x00367350;

		enum class Status : u8
		{
			Rewritten,
			AlreadyNative,
			Absent,
			// The symbol names other bytes, such as a file since unloaded.
			Foreign,
			Unreadable,
		};

		struct Access
		{
			std::function<bool(u32, void*, u32)> read;
			std::function<bool(u32, const void*, u32)> write;

			static Access Guest();
		};

		// A public whose PS2 text tells the player to press START to confirm.
		struct ConfirmPrompt
		{
			u32 symbol = 0;
			std::string_view original;
		};
		static inline constexpr std::string_view ConfirmButton = "START button";
		static inline constexpr std::array<ConfirmPrompt, 3> ConfirmPrompts{{
			{0x9BD83674, "Press START button"},
			// GLevelLoadErrorMenu's text, and its text when CProfile has no save target.
			{0x421389F5, "Error Loading Level\nPress START button to continue"},
			{0x1CE48D9B, "Error Loading Level!\nA memory card (8MB) (for PlayStation\xac"
						 "2)\nis not inserted into MEMORY CARD slot 1\nPress START button to continue"},
		}};

		// Names the confirm key in place of ConfirmButton.
		static Status RewriteConfirmPrompt(const ConfirmPrompt& prompt, std::string_view confirm_label, const Access& access);
		static void ObserveSetupPublicsExit();
	};
} // namespace AVPE
