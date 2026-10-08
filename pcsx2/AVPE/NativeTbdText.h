// PC wording for AVP:E's PS2-specific TBD strings. Fork-local.

#pragma once

#include "AVPE/NativeMenuInput.h"

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

		// The title's "Press START button" (public 0x9BD83674) names the confirm key.
		static Status RewriteTitlePrompt(std::string_view confirm_label, const Access& access);
		static void ObserveSetupPublicsExit();
	};
} // namespace AVPE
