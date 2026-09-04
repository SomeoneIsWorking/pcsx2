// Grounded EALOGO movie service-census boundary. Fork-local; not for upstream PCSX2.

#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace AVPE::NativeMovieBiosBoundary
{
	void Reset();
	void ObserveNativeOpen(std::string_view canonical_path);
	void ObserveNativeClose(std::string_view canonical_path);
	// Call after recording the handled IOP import so ioman.close is retained.
	void ObserveHandledIopImport(std::string_view library, std::string_view function);
	// Returns a structured complete or incomplete capture; timeout is never an
	// empty successful census.
	std::string CaptureJson(std::chrono::milliseconds timeout);
} // namespace AVPE::NativeMovieBiosBoundary
