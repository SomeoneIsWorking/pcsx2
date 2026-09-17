// Diagnostic HTTP presentation for live AVP:E font-render observations. Fork-local.
#pragma once

#include <lucent/http.h>

#include <optional>
#include <string>

namespace AVPE::NativePromptRoute
{
	std::optional<lucent::http::Response> Handle(const lucent::http::Request& request);
	std::string SnapshotJson();
} // namespace AVPE::NativePromptRoute
