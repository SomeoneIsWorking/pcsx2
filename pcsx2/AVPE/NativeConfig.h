// AVP:E immutable runtime configuration. Fork-local; not for upstream PCSX2.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace AVPE::NativeConfig
{
	// Initializes the one immutable AVP:E configuration snapshot. Callers use
	// the narrow accessors below rather than reading process environment state.
	void Initialize();

	bool BiosTraceEnabled();
	bool BiosMovieTraceRequested();
	std::string_view ControlNonce();
	std::uint16_t ControlPort();

	std::optional<std::string_view> AssetByteTraceMode();
	bool AssetByteTraceIsUnset();
	std::optional<std::string_view> LoadTimingMode();
	bool MissionLoadTimingTargetRequested();

	std::string_view NativeAssetRoot();
	std::string_view NativeAssetManifestSha256();
} // namespace AVPE::NativeConfig
