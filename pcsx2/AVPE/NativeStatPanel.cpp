// The in-game unit status panel follows the selection, as in a PC RTS. Fork-local.

#include "AVPE/NativeStatPanel.h"

#include "AVPE/GuestObjects.h"
#include "vtlb.h"

namespace AVPE
{
	bool NativeStatPanel::ObserveHoverResult(const u32 sp, const Access& access)
	{
		const u32 result = sp + HoverResultStackOffset;
		u32 hovered = 0;
		u32 pointer = 0;
		u32 selection = 0;
		u32 data = 0;
		u32 count = 0;
		u32 mark = 0;
		u32 object = 0;
		u32 handle = 0;
		return access.read(result, &hovered) && hovered == 0 && access.read(PointerInstance, &pointer) &&
		       pointer != 0 && access.read(pointer + SelectionOffset, &selection) && selection != 0 &&
		       access.read(selection, &data) && access.read(selection + 4, &count) && count != 0 && data != 0 &&
		       access.read(data, &mark) && mark != 0 && access.read(mark + MarkObjectOffset, &object) &&
		       object != 0 && access.read(object + ObjectHandleOffset, &handle) && handle != 0 &&
		       access.write(result, handle);
	}

	NativeStatPanel::Access NativeStatPanel::Access::Guest()
	{
		return {
			.read = GuestObjects::ReadWord,
			.write = [](const u32 address, const u32 value) { return vtlb_memSafeWriteBytes(address, &value, sizeof(value)); },
		};
	}
} // namespace AVPE
