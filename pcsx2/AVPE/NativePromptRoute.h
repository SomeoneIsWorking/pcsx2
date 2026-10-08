// Diagnostic HTTP presentation for live AVP:E font-render and prompt-placement observations. Fork-local.
#pragma once

#include <lucent/http.h>

#include <optional>
#include <string>

namespace AVPE::NativePromptRoute
{
	std::optional<lucent::http::Response> Handle(const lucent::http::Request& request);
	std::string SnapshotJson();
	std::string PlacementJson();
} // namespace AVPE::NativePromptRoute
