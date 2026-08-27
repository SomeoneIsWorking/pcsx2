// AVP:E native asset save-state diagnostics. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssetStateSnapshot.h"

#include "AVPE/NativeAssets.h"
#include "AVPE/NativeCdvdCompletion.h"
#include "IopBios.h"

#include <string_view>

namespace AVPE::NativeAssetStateSnapshot
{
	namespace
	{
		void AppendJsonString(std::string& output, const std::string_view value)
		{
			constexpr char kHexDigits[] = "0123456789abcdef";
			output.push_back('"');
			for (const unsigned char character : value)
			{
				switch (character)
				{
					case '"':
						output += "\\\"";
						break;
					case '\\':
						output += "\\\\";
						break;
					case '\b':
						output += "\\b";
						break;
					case '\f':
						output += "\\f";
						break;
					case '\n':
						output += "\\n";
						break;
					case '\r':
						output += "\\r";
						break;
					case '\t':
						output += "\\t";
						break;
					default:
						if (character < 0x20)
						{
							output += "\\u00";
							output.push_back(kHexDigits[character >> 4]);
							output.push_back(kHexDigits[character & 0x0f]);
						}
						else
						{
							output.push_back(static_cast<char>(character));
						}
						break;
				}
			}
			output.push_back('"');
		}
	} // namespace

	std::string CaptureJsonOnCPUThread()
	{
		const std::vector<R3000A::ioman::NativeAssetHandleState> descriptors =
			R3000A::ioman::getNativeAssetHandleState();
		const std::vector<NativeAssets::CdvdMappingState> mappings = NativeAssets::GetCdvdMappingState();
		const u32 next_lsn = NativeAssets::GetNextCdvdLsn();
		const NativeCdvdCompletion::Snapshot completion = NativeCdvdCompletion::GetSnapshot();

		std::string body = "{\"descriptors\":[";
		for (std::size_t index = 0; index < descriptors.size(); ++index)
		{
			if (index != 0)
				body.push_back(',');
			const R3000A::ioman::NativeAssetHandleState& descriptor = descriptors[index];
			body += "{\"fd\":" + std::to_string(descriptor.fd) + ",\"path\":";
			AppendJsonString(body, descriptor.path);
			body += ",\"cursor\":" + std::to_string(descriptor.cursor) + '}';
		}

		body += "],\"cdvd_mappings\":[";
		for (std::size_t index = 0; index < mappings.size(); ++index)
		{
			if (index != 0)
				body.push_back(',');
			const NativeAssets::CdvdMappingState& mapping = mappings[index];
			body += "{\"path\":";
			AppendJsonString(body, mapping.guest_path);
			body += ",\"base_lsn\":" + std::to_string(mapping.base_lsn);
			body += ",\"size\":" + std::to_string(mapping.size) + ",\"sha256\":";
			AppendJsonString(body, mapping.sha256);
			body.push_back('}');
		}

		body += "],\"next_lsn\":" + std::to_string(next_lsn);
		body += ",\"cdvd_completion_active_tokens\":" + std::to_string(completion.active_tokens) + '}';
		return body;
	}
} // namespace AVPE::NativeAssetStateSnapshot
