// AVP:E native guest-reset control boundary. Fork-local; not for upstream PCSX2.

#pragma once

#include <lucent/http.h>

namespace AVPE::NativeGuestReset
{
	lucent::http::Response Handle();
} // namespace AVPE::NativeGuestReset
