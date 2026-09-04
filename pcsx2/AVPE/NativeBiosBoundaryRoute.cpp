// AVP:E grounded BIOS-boundary control routes. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeBiosBoundaryRoute.h"

#include "AVPE/NativeGameLoadBoundary.h"
#include "AVPE/NativeGameSaveBoundary.h"
#include "AVPE/NativeBiosTrace.h"
#include "AVPE/NativeMovieBiosBoundary.h"
#include "AVPE/NativeShellShutdownBoundary.h"
#include "Host.h"

#include <chrono>

namespace AVPE::NativeBiosBoundaryRoute
{
	namespace
	{
		lucent::http::Response StartBoundary(const char* const name, const char* const error,
			const auto&& start)
		{
			bool started = false;
			Host::RunOnCPUThread([&started, &start]() { started = start(); }, true);
			if (!started)
				return lucent::http::Response::text(409, "Conflict", error);
			return lucent::http::Response::json(200, "OK",
				std::string("{\"started\":true,\"boundary\":\"") + name + "\"}");
		}

		lucent::http::Response CaptureBoundary(const auto&& capture)
		{
			const std::string snapshot = capture(std::chrono::seconds(20));
			if (snapshot.find("\"complete\":true") == std::string::npos)
				return lucent::http::Response::json(504, "Gateway Timeout", snapshot);
			return lucent::http::Response::json(200, "OK", snapshot);
		}
	} // namespace

	lucent::http::Response StartGameLoad()
	{
		return StartBoundary("cprofile-load-game",
			"grounded game-load boundary requires the supported AVP:E control-test target\n",
			[]() { return NativeGameLoadBoundary::Start(); });
	}

	lucent::http::Response CaptureGameLoad()
	{
		return CaptureBoundary([](const auto timeout) { return NativeGameLoadBoundary::CaptureJson(timeout); });
	}

	lucent::http::Response StartGameSave()
	{
		return StartBoundary("cprofile-save-game",
			"grounded game-save boundary requires the supported AVP:E control-test target\n",
			[]() { return NativeGameSaveBoundary::Start(); });
	}

	lucent::http::Response CaptureGameSave()
	{
		return CaptureBoundary([](const auto timeout) { return NativeGameSaveBoundary::CaptureJson(timeout); });
	}

	lucent::http::Response StartShellShutdown()
	{
		return StartBoundary("cshell-quit-main-loop-return",
			"grounded shell-shutdown boundary requires the supported AVP:E control-test target and live shell\n",
			[]() { return NativeShellShutdownBoundary::Start(); });
	}

	lucent::http::Response CaptureShellShutdown()
	{
		return CaptureBoundary([](const auto timeout) { return NativeShellShutdownBoundary::CaptureJson(timeout); });
	}

	lucent::http::Response CaptureAtGuestBoundary()
	{
		const std::string snapshot = NativeBiosTrace::CaptureAtGuestBoundaryJson(std::chrono::seconds(5));
		if (snapshot.empty())
			return lucent::http::Response::text(504, "Gateway Timeout",
				"guest CPU frame boundary was not observed before the BIOS trace deadline\n");
		return lucent::http::Response::json(200, "OK", snapshot);
	}

	lucent::http::Response CaptureMovie()
	{
		const std::string snapshot = NativeMovieBiosBoundary::CaptureJson(std::chrono::seconds(30));
		if (snapshot.empty())
			return lucent::http::Response::text(504, "Gateway Timeout",
				"native EALOGO movie close was not observed before the BIOS trace deadline\n");
		if (snapshot.find("\"complete\":false") != std::string::npos)
			return lucent::http::Response::json(504, "Gateway Timeout", snapshot);
		return lucent::http::Response::json(200, "OK", snapshot);
	}
} // namespace AVPE::NativeBiosBoundaryRoute
