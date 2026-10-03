// Diagnostic HTTP presentation for live AVP:E render-dispatch and screen-bounds
// observations. Fork-local.

#include "AVPE/NativeMeshBoundsRoute.h"

#include "AVPE/AVPE.h"
#include "AVPE/NativeMeshBoundsTrace.h"
#include "VMManager.h"

#include <fmt/format.h>
#include <rapidjson/document.h>

#include <array>
#include <optional>
#include <string_view>

namespace AVPE::NativeMeshBoundsRoute
{
	namespace
	{
		constexpr std::string_view TargetSerial = "SLUS-20147";
		constexpr u32 TargetCrc = 0x64DA78A3;

		std::optional<u32> ParseResourceAddress(const rapidjson::Value& entry)
		{
			if (!entry.IsString())
			{
				return std::nullopt;
			}
			const std::string_view text(entry.GetString(), entry.GetStringLength());
			if (text.size() <= 2 || text.size() > 10 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X'))
			{
				return std::nullopt;
			}
			u32 value = 0;
			for (size_t digit = 2; digit < text.size(); ++digit)
			{
				const char character = text[digit];
				u32 nibble = 0;
				if (character >= '0' && character <= '9')
				{
					nibble = static_cast<u32>(character - '0');
				}
				else if (character >= 'a' && character <= 'f')
				{
					nibble = static_cast<u32>(character - 'a') + 10U;
				}
				else if (character >= 'A' && character <= 'F')
				{
					nibble = static_cast<u32>(character - 'A') + 10U;
				}
				else
				{
					return std::nullopt;
				}
				value = (value << 4) | nibble;
			}
			if (value == 0)
			{
				return std::nullopt;
			}
			return value;
		}

		// The observer admits only the resources the caller names, so a zero match
		// count means the named resource was not drawn rather than that a bounded
		// table filled up first.
		bool AdmittedResources(const lucent::http::Request& request,
			std::array<u32, NativeMeshBoundsTrace::MaxAdmittedResources>& resources, size_t& count)
		{
			count = 0;
			const std::string body = request.body;
			rapidjson::Document document;
			document.Parse(body.data(), body.size());
			if (!document.IsObject())
			{
				return false;
			}
			const auto members = document.FindMember("resources");
			if (members == document.MemberEnd() || !members->value.IsArray())
			{
				return false;
			}
			for (const auto& entry : members->value.GetArray())
			{
				if (count >= resources.size())
				{
					break;
				}
				const std::optional<u32> address = ParseResourceAddress(entry);
				if (!address)
				{
					return false;
				}
				resources[count++] = *address;
			}
			return count != 0;
		}
	} // namespace

	std::string SnapshotJson()
	{
		const NativeMeshBoundsTrace::Snapshot snapshot = NativeMeshBoundsTrace::Process().Capture();
		std::string output = fmt::format(
			R"({{"schema":"avpe-mesh-bounds-v5","armed":{},"observed_dispatches":{},"matched_dispatches":{},"observed_rects":{},"matched_rects":{},"invalid_dispatches":{},"invalid_reads":{},"dropped_samples":{},"sample_capacity":{},"admitted":[)",
			snapshot.armed, snapshot.observed_dispatches, snapshot.matched_dispatches, snapshot.observed_rects,
			snapshot.matched_rects, snapshot.invalid_dispatches, snapshot.invalid_reads,
			snapshot.dropped_samples, NativeMeshBoundsTrace::MaxSamples);
		for (size_t index = 0; index < snapshot.admitted.size(); ++index)
		{
			if (index != 0)
			{
				output.push_back(',');
			}
			output += fmt::format(R"("0x{:08X}")", snapshot.admitted[index]);
		}
		output += R"(],"dispatches":[)";
		for (size_t index = 0; index < snapshot.dispatches.size(); ++index)
		{
			const auto& dispatch = snapshot.dispatches[index];
			if (index != 0)
			{
				output.push_back(',');
			}
			output += fmt::format(
				R"({{"resource":"0x{:08X}","render_node":"0x{:08X}","render":"0x{:08X}","workspace":"0x{:08X}","get_matrix":"0x{:08X}","calls":{}}})",
				dispatch.resource, dispatch.render_node, dispatch.render, dispatch.workspace,
				dispatch.get_matrix, dispatch.calls);
		}
		output += R"(],"samples":[)";
		for (size_t index = 0; index < snapshot.samples.size(); ++index)
		{
			const auto& sample = snapshot.samples[index];
			if (index != 0)
			{
				output.push_back(',');
			}
			output += fmt::format(
				R"({{"workspace":"0x{:08X}","render":"0x{:08X}","bounds_object":"0x{:08X}","xmin":{},"ymin":{},"xmax":{},"ymax":{},"calls":{}}})",
				sample.workspace, sample.render, sample.bounds_object, sample.xmin, sample.ymin,
				sample.xmax, sample.ymax, sample.calls);
		}
		output += "]}";
		return output;
	}

	std::optional<lucent::http::Response> Handle(const lucent::http::Request& request)
	{
		const auto path = request.path();
		if (path == "/mesh/bounds-trace/stop" && request.method == "POST")
		{
			NativeMeshBoundsTrace::Process().Stop();
			return lucent::http::Response::json(200, "OK", SnapshotJson());
		}
		if (path != "/mesh/bounds-trace")
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
					R"({"error":"mesh bounds trace requires the supported AVP:E control-test target"})");
			}
			std::array<u32, NativeMeshBoundsTrace::MaxAdmittedResources> resources{};
			size_t count = 0;
			if (!AdmittedResources(request, resources, count))
			{
				return lucent::http::Response::json(400, "Bad Request",
					R"({"error":"arm the mesh bounds trace with a non-empty resources array of 0x addresses"})");
			}
			NativeMeshBoundsTrace::Process().Start(std::span<const u32>(resources.data(), count));
			return lucent::http::Response::json(200, "OK", SnapshotJson());
		}
		return std::nullopt;
	}
} // namespace AVPE::NativeMeshBoundsRoute