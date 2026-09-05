// AVP:E immutable runtime configuration. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeConfig.h"

#include <lucent/config.h>

#include <string>

namespace AVPE::NativeConfig
{
	namespace
	{
		enum class DiagnosticMode : u8
		{
			Disabled,
			Oracle,
			Native,
		};

		struct Settings
		{
			bool bios_trace_enabled = true;
			bool bios_movie_trace_requested = false;
			std::string control_nonce;
			std::uint16_t control_port = 28447;
			DiagnosticMode asset_byte_trace_mode = DiagnosticMode::Disabled;
			bool asset_byte_trace_is_unset = true;
			DiagnosticMode load_timing_mode = DiagnosticMode::Disabled;
			bool mission_load_timing_target_requested = false;
			std::string native_asset_root;
			std::string native_asset_manifest_sha256;
		};

		DiagnosticMode ParseDiagnosticMode(const std::string_view configured)
		{
			if (configured == "oracle")
				return DiagnosticMode::Oracle;
			if (configured == "native")
				return DiagnosticMode::Native;
			return DiagnosticMode::Disabled;
		}

		std::optional<std::string_view> ModeName(const DiagnosticMode mode)
		{
			switch (mode)
			{
				case DiagnosticMode::Oracle:
					return "oracle";
				case DiagnosticMode::Native:
					return "native";
				case DiagnosticMode::Disabled:
					return std::nullopt;
			}
			return std::nullopt;
		}

		const Settings& GetSettings()
		{
			static const Settings settings = []() {
				lucent::config::set_prefix("AVPE_");
				Settings loaded;
				loaded.bios_trace_enabled = lucent::config::text("BIOS_TRACE") != "0";
				loaded.bios_movie_trace_requested = lucent::config::text("BIOS_MOVIE_TRACE") == "1";
				loaded.control_nonce = lucent::config::text("CONTROL_NONCE");

				const long configured_port = lucent::config::number("HTTP_PORT", loaded.control_port);
				if (configured_port > 0 && configured_port <= 65535)
					loaded.control_port = static_cast<std::uint16_t>(configured_port);

				loaded.asset_byte_trace_is_unset = !lucent::config::present("ASSET_BYTE_TRACE");
				loaded.asset_byte_trace_mode = ParseDiagnosticMode(lucent::config::text("ASSET_BYTE_TRACE"));
				loaded.load_timing_mode = ParseDiagnosticMode(lucent::config::text("LOAD_TIMING"));
				loaded.mission_load_timing_target_requested = lucent::config::text("LOAD_TIMING_TARGET") == "mission";
				loaded.native_asset_root = lucent::config::text("NATIVE_ASSET_ROOT");
				loaded.native_asset_manifest_sha256 = lucent::config::text("NATIVE_ASSET_MANIFEST_SHA256");
				return loaded;
			}();
			return settings;
		}
	} // namespace

	void Initialize()
	{
		(void)GetSettings();
	}

	bool BiosTraceEnabled()
	{
		return GetSettings().bios_trace_enabled;
	}

	bool BiosMovieTraceRequested()
	{
		return GetSettings().bios_movie_trace_requested;
	}

	std::string_view ControlNonce()
	{
		return GetSettings().control_nonce;
	}

	std::uint16_t ControlPort()
	{
		return GetSettings().control_port;
	}

	std::optional<std::string_view> AssetByteTraceMode()
	{
		return ModeName(GetSettings().asset_byte_trace_mode);
	}

	bool AssetByteTraceIsUnset()
	{
		return GetSettings().asset_byte_trace_is_unset;
	}

	std::optional<std::string_view> LoadTimingMode()
	{
		return ModeName(GetSettings().load_timing_mode);
	}

	bool MissionLoadTimingTargetRequested()
	{
		return GetSettings().mission_load_timing_target_requested;
	}

	std::string_view NativeAssetRoot()
	{
		return GetSettings().native_asset_root;
	}

	std::string_view NativeAssetManifestSha256()
	{
		return GetSettings().native_asset_manifest_sha256;
	}
} // namespace AVPE::NativeConfig
