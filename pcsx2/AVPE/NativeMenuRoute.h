// AVP:E native menu control routes. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/NativeMenuInput.h"

#include <string>
#include <optional>

namespace lucent::http
{
	struct Response;
	struct Request;
} // namespace lucent::http

namespace AVPE::NativeMenuRoute
{
	std::optional<lucent::http::Response> Handle(const lucent::http::Request& request);
	std::string FormatActionResponse(const std::string& action_name, const NativeMenuInput::Result& result);
	lucent::http::Response HandleAction(const std::string& body);
	lucent::http::Response HandleState();
	lucent::http::Response HandleReadiness();
	lucent::http::Response HandleMovieState();
} // namespace AVPE::NativeMenuRoute
