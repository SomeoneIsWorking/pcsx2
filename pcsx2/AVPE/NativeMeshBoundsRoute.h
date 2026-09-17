// Diagnostic HTTP presentation for live AVP:E mesh screen-bounds observations. Fork-local.
#pragma once

#include <lucent/http.h>

#include <optional>
#include <string>

namespace AVPE::NativeMeshBoundsRoute
{
	std::optional<lucent::http::Response> Handle(const lucent::http::Request& request);
	std::string SnapshotJson();
} // namespace AVPE::NativeMeshBoundsRoute
