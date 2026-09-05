// AVP:E guest pad polling readiness. Fork-local; not for upstream PCSX2.

#include "AVPE/NativePadReadiness.h"

namespace AVPE::NativePadReadiness
{
	namespace
	{
		bool HasDigitalReport(const u32 report)
		{
			// libpad status byte followed by DS2 mode/length. Require a real
			// digital report, not the neutral initialization buffer or 0xff fill.
			const u32 header = report & 0xFFFF;
			return header == 0x4100 || header == 0x7300 || header == 0x7900;
		}
	} // namespace

	bool IsReady(const u32 input_device, const ReadWord& read)
	{
		u32 device_vtable = 0;
		u32 backend = 0;
		u32 backend_vtable = 0;
		if (!GuestObjects::IsPlausibleAddress(input_device) || !read(input_device, &device_vtable) ||
			device_vtable != 0x00334870 || !read(input_device + 0x34, &backend) ||
			!GuestObjects::IsPlausibleAddress(backend) || !read(backend, &backend_vtable) ||
			backend_vtable != 0x003348C0)
			return false;

		u32 port = 0;
		u32 slot = 0;
		u32 flags = 0;
		u32 state = 0;
		u32 previous = 0;
		u32 current = 0;
		if (!read(backend + 0x404, &port) || !read(backend + 0x408, &slot) ||
			!read(backend + 0x40C, &flags) || !read(backend + 0x414, &state) ||
			!read(backend + 0x41C, &previous) || !read(backend + 0x43C, &current))
			return false;

		// PollDevices (0x0017dde0): active, reading, and at least one successful
		// read; no pending state/mode negotiation. Only the low flags byte is owned.
		constexpr u32 ready_flags = 0x01 | 0x10 | 0x40;
		constexpr u32 negotiating_flags = 0x02 | 0x04 | 0x08;
		return port == 0 && slot == 0 && (flags & ready_flags) == ready_flags &&
		       (flags & negotiating_flags) == 0 && (state == 2 || state == 6) &&
		       HasDigitalReport(previous) && HasDigitalReport(current);
	}
} // namespace AVPE::NativePadReadiness
