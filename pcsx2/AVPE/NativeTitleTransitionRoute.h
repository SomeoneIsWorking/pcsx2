// AVP:E title-transition diagnostic HTTP route. Fork-local; not for upstream PCSX2.

#pragma once

#include <lucent/http.h>

namespace AVPE::NativeTitleTransitionRoute
{
	lucent::http::Response Start();
	lucent::http::Response Snapshot();
} // namespace AVPE::NativeTitleTransitionRoute
