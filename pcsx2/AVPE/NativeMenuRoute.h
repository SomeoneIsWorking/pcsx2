// AVP:E native menu control routes. Fork-local; not for upstream PCSX2.

#pragma once

#include <lucent/http.h>

#include <string>

namespace AVPE::NativeMenuRoute
{
	lucent::http::Response HandleAction(const std::string& body);
	lucent::http::Response HandleState();
	lucent::http::Response HandleReadiness();
} // namespace AVPE::NativeMenuRoute
