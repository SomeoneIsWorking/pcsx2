// AVPE camera control route serialization. Fork-local; not for upstream PCSX2.

#pragma once

#include <lucent/http.h>

#include <string>

namespace AVPE::NativeCameraRoute
{
	lucent::http::Response Handle(const std::string& body);
} // namespace AVPE::NativeCameraRoute
