// Mission drag-box selection as in a PC RTS. Fork-local.

#include "AVPE/NativeDragSelect.h"

#include "AVPE/GuestObjects.h"
#include "vtlb.h"

#include <algorithm>
#include <bit>

namespace AVPE
{
	namespace
	{
		NativeDragSelect s_process_drag_select;

		bool ReadPoint(const NativeDragSelect::Access& access, const u32 address, float* x, float* y)
		{
			u32 x_bits = 0;
			u32 y_bits = 0;
			if (!access.read(address, &x_bits) || !access.read(address + 4, &y_bits))
			{
				return false;
			}
			*x = std::bit_cast<float>(x_bits);
			*y = std::bit_cast<float>(y_bits);
			return true;
		}

		bool WritePoint(const NativeDragSelect::Access& access, const u32 address, const float x, const float y)
		{
			return access.write(address, std::bit_cast<u32>(x)) && access.write(address + 4, std::bit_cast<u32>(y));
		}
	} // namespace

	void NativeDragSelect::ObserveGrowBox(const Access& access)
	{
		u32 pointer = 0;
		u32 growing = 0;
		u32 input_type = 0;
		if (!access.read(PointerInstance, &pointer) || pointer == 0 ||
			!access.read(pointer + GrowingOffset, &growing) || (growing & 0xFF) == 0 ||
			!access.read(pointer + InputTypeOffset, &input_type) || input_type != AbsoluteInputType)
		{
			m_anchor.reset();
			return;
		}
		if (!m_anchor.has_value())
		{
			// The box starts at the press point and has grown less than a frame's step.
			Point anchor{};
			if (!ReadPoint(access, pointer + BoxMinOffset, &anchor.x, &anchor.y))
			{
				return;
			}
			m_anchor = anchor;
		}
		Point cursor{};
		if (!ReadPoint(access, pointer + CursorOffset, &cursor.x, &cursor.y))
		{
			return;
		}
		WritePoint(access, pointer + BoxMinOffset, std::min(m_anchor->x, cursor.x), std::min(m_anchor->y, cursor.y));
		WritePoint(access, pointer + BoxMaxOffset, std::max(m_anchor->x, cursor.x), std::max(m_anchor->y, cursor.y));
	}

	void NativeDragSelect::Reset()
	{
		m_anchor.reset();
	}

	NativeDragSelect& NativeDragSelect::Process()
	{
		return s_process_drag_select;
	}

	NativeDragSelect::Access NativeDragSelect::Access::Guest()
	{
		return {
			.read = GuestObjects::ReadWord,
			.write = [](const u32 address, const u32 value) { return vtlb_memSafeWriteBytes(address, &value, sizeof(value)); },
		};
	}
} // namespace AVPE
