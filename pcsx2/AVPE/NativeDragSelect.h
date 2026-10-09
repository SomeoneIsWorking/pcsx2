// Mission drag-box selection as in a PC RTS. Fork-local.

#pragma once

#include "common/Pcsx2Defs.h"

#include <functional>
#include <optional>

namespace AVPE
{
	// A held select grows GAvPPointer's box around the press point every frame
	// (UpdateGrowBox), the pad's selection gesture. For the mouse the box instead spans the
	// press point and the cursor, as a dragged rectangle; the guest's release selects in it.
	class NativeDragSelect final
	{
	public:
		// GAvPPointer::Process, after UpdateGrowBox returns.
		static inline constexpr u32 GrowBoxReturnPc = 0x001B1CC4;
		static inline constexpr u32 PointerInstance = 0x00367720;
		// Screen position, in guest pixels.
		static inline constexpr u32 CursorOffset = 0x40;
		static inline constexpr u32 BoxMinOffset = 0x188;
		static inline constexpr u32 BoxMaxOffset = 0x194;
		static inline constexpr u32 GrowingOffset = 0x1B8;
		static inline constexpr u32 InputTypeOffset = 0x224;
		// SetInputType(1): the absolute pointer the mouse drives.
		static inline constexpr u32 AbsoluteInputType = 1;

		struct Access
		{
			std::function<bool(u32, u32*)> read;
			std::function<bool(u32, u32)> write;

			static Access Guest();
		};

		// EE thread, at GrowBoxReturnPc.
		void ObserveGrowBox(const Access& access);
		void Reset();

		static NativeDragSelect& Process();

	private:
		struct Point
		{
			float x;
			float y;
		};

		std::optional<Point> m_anchor;
	};
} // namespace AVPE
