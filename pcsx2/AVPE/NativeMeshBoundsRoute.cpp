// Diagnostic HTTP presentation for live AVP:E mesh screen-bounds observations. Fork-local.

#include "AVPE/NativeMeshBoundsRoute.h"

#include "AVPE/AVPE.h"
#include "AVPE/NativeMeshBoundsTrace.h"
#include "VMManager.h"

#include <fmt/format.h>

#include <string_view>

namespace AVPE::NativeMeshBoundsRoute
{
	namespace
	{
		constexpr std::string_view TargetSerial = "SLUS-20147";
		constexpr u32 TargetCrc = 0x64DA78A3;
	} // namespace

	std::string SnapshotJson()
	{
		const NativeMeshBoundsTrace::Snapshot snapshot = NativeMeshBoundsTrace::Process().Capture();
		std::string output = fmt::format(
			R"({{"schema":"avpe-mesh-bounds-v1","armed":{},"observed_calls":{},"invalid_reads":{},"dropped_samples":{},"sample_capacity":{},"samples":[)",
			snapshot.armed, snapshot.observed_calls, snapshot.invalid_reads, snapshot.dropped_samples,
			NativeMeshBoundsTrace::MaxSamples);
		for (size_t index = 0; index < snapshot.samples.size(); ++index)
		{
			const auto& sample = snapshot.samples[index];
			if (index != 0)
			{
				output.push_back(',');
			}
			output += fmt::format(
				R"({{"workspace":"0x{:08X}","render":"0x{:08X}","bounds_object":"0x{:08X}","xmax":{},"ymax":{},"xmin":{},"ymin":{},"calls":{}}})",
				sample.workspace, sample.render, sample.bounds_object, sample.xmax, sample.ymax,
				sample.xmin, sample.ymin, sample.calls);
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
			NativeMeshBoundsTrace::Process().Start();
			return lucent::http::Response::json(200, "OK", SnapshotJson());
		}
		return std::nullopt;
	}
} // namespace AVPE::NativeMeshBoundsRoute
