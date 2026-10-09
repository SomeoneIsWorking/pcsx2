// The in-game unit status panel follows the selection, as in a PC RTS. Fork-local.

#pragma once

#include "common/Pcsx2Defs.h"

#include <functional>

namespace AVPE
{
	// GInGameStatMenu::Process shows the object GAvPPointer::GetOneSelectedGobject(0, 0)
	// returns, which is the first object under the pointer, so a selected unit's panel
	// vanishes once the pointer leaves it and never appears after a group recall. When
	// nothing is under the pointer the panel takes the first selected unit instead.
	class NativeStatPanel final
	{
	public:
		// Return from that call; the HGOBJECT result is at [sp+0x50].
		static inline constexpr u32 HoverResultPc = 0x00281DFC;
		static inline constexpr u32 HoverResultStackOffset = 0x50;
		static inline constexpr u32 PointerInstance = 0x00367720;
		// GAvPPointer's selection ZArray {data, count}; a GMarkSelect holds its object at +0xA8.
		static inline constexpr u32 SelectionOffset = 0x1B0;
		static inline constexpr u32 MarkObjectOffset = 0xA8;
		static inline constexpr u32 ObjectHandleOffset = 0x18;

		struct Access
		{
			std::function<bool(u32, u32*)> read;
			std::function<bool(u32, u32)> write;

			static Access Guest();
		};

		// True when the panel was given the selected unit.
		static bool ObserveHoverResult(u32 sp, const Access& access);
	};
} // namespace AVPE
