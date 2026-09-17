// Diagnostic HTTP presentation for live AVP:E font-render observations. Fork-local.

#include "AVPE/NativePromptRoute.h"

#include "AVPE/AVPE.h"
#include "AVPE/NativePromptTrace.h"
#include "VMManager.h"

#include <fmt/format.h>

#include <span>
#include <string_view>

namespace AVPE::NativePromptRoute
{
	namespace
	{
		constexpr std::string_view TargetSerial = "SLUS-20147";
		constexpr u32 TargetCrc = 0x64DA78A3;
		constexpr char HexDigits[] = "0123456789abcdef";

		void AppendHex(std::string& output, const std::span<const u8> bytes)
		{
			for (const u8 byte : bytes)
			{
				output.push_back(HexDigits[byte >> 4]);
				output.push_back(HexDigits[byte & 15]);
			}
		}
	} // namespace

	std::string SnapshotJson()
	{
		const NativePromptTrace::Snapshot snapshot = NativePromptTrace::Process().Capture();
		std::string output = fmt::format(
			R"({{"schema":"avpe-font-render-v1","armed":{},"observed_calls":{},"invalid_reads":{},"dropped_fonts":{},"dropped_texts":{},"truncated_text_calls":{},"font_capacity":{},"text_capacity":{},"fonts":[)",
			snapshot.armed, snapshot.observed_calls, snapshot.invalid_reads, snapshot.dropped_fonts,
			snapshot.dropped_texts, snapshot.truncated_text_calls, NativePromptTrace::MaxFonts,
			NativePromptTrace::MaxTexts);
		for (size_t index = 0; index < snapshot.fonts.size(); ++index)
		{
			const auto& font = snapshot.fonts[index];
			if (index != 0)
			{
				output.push_back(',');
			}
			output += fmt::format(R"({{"address":"0x{:08X}","calls":{},"glyph_map_hex":")",
				font.address, font.calls);
			AppendHex(output, font.glyph_map);
			output += R"("})";
		}
		output += R"(],"texts":[)";
		for (size_t index = 0; index < snapshot.texts.size(); ++index)
		{
			const auto& sample = snapshot.texts[index];
			if (index != 0)
			{
				output.push_back(',');
			}
			output += fmt::format(
				R"({{"font_index":{},"render":"0x{:08X}","resource":"0x{:08X}","length":{},"truncated":{},"calls":{},"text_hex":")",
				sample.font_index, sample.render, sample.resource, sample.length,
				sample.truncated, sample.calls);
			AppendHex(output, std::span(sample.text).first(sample.length));
			output += R"("})";
		}
		output += "]}";
		return output;
	}

	std::optional<lucent::http::Response> Handle(const lucent::http::Request& request)
	{
		const auto path = request.path();
		if (path == "/prompt/font-trace/stop" && request.method == "POST")
		{
			NativePromptTrace::Process().Stop();
			return lucent::http::Response::json(200, "OK", SnapshotJson());
		}
		if (path != "/prompt/font-trace")
		{
			return std::nullopt;
		}
		if (request.method == "GET")
		{
			return lucent::http::Response::json(200, "OK", SnapshotJson());
		}
		if (request.method == "POST")
		{
			if (!IsSurfacelessControlTest() || !VMManager::HasValidVM() ||
				VMManager::GetDiscSerial() != TargetSerial || VMManager::GetDiscCRC() != TargetCrc)
			{
				return lucent::http::Response::json(409, "Conflict",
					R"({"error":"font trace requires the supported AVP:E control-test target"})");
			}
			NativePromptTrace::Process().Start();
			return lucent::http::Response::json(200, "OK", SnapshotJson());
		}
		return std::nullopt;
	}
} // namespace AVPE::NativePromptRoute
