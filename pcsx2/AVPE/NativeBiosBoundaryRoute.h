// AVP:E grounded BIOS-boundary control routes. Fork-local; not for upstream PCSX2.

#pragma once

#include <lucent/http.h>

namespace AVPE::NativeBiosBoundaryRoute
{
	lucent::http::Response StartGameLoad();
	lucent::http::Response CaptureGameLoad();
	lucent::http::Response StartGameSave();
	lucent::http::Response CaptureGameSave();
	lucent::http::Response StartShellShutdown();
	lucent::http::Response CaptureShellShutdown();
	lucent::http::Response CaptureAtGuestBoundary();
	lucent::http::Response CaptureMovie();
} // namespace AVPE::NativeBiosBoundaryRoute
