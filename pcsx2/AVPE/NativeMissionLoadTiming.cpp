// AVP:E mission-load timing evidence. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMissionLoadTiming.h"

#include "AVPE/AVPE.h"
#include "AVPE/GuestObjects.h"
#include "AVPE/LoadTimingPoint.h"
#include "R5900.h"
#include "VMManager.h"

#include <array>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string_view>

namespace AVPE::NativeMissionLoadTiming
{
	namespace
	{
		constexpr std::string_view kSchema = "avpe-mission-load-timing-v1";
		constexpr std::string_view kTargetSerial = "SLUS-20147";
		constexpr u32 kTargetCrc = 0x64DA78A3;
		constexpr std::string_view kModeEnvironment = "AVPE_LOAD_TIMING";
		constexpr std::string_view kTargetEnvironment = "AVPE_LOAD_TIMING_TARGET";
		constexpr std::string_view kMissionTarget = "mission";
		constexpr std::string_view kMissionPath = "M01/background.tbd";
		constexpr u32 kShellLoadLevelPc = 0x0016F910;
		// Grounded post-return loop entry: MainLoop calls ShellLoadLevel at
		// 0x16f784 and begins its next iteration at 0x16f744 after the call
		// returns. This avoids delay-slot and recompiler return-block edges.
		constexpr u32 kShellLoadLevelReturnPc = 0x0016F744;
		constexpr u32 kShellSingletonAddress = 0x003672F0;

		std::mutex s_mutex;
		std::optional<LoadTimingPoint::Point> s_start;
		std::optional<LoadTimingPoint::Point> s_end;
		u64 s_ordinal = 0;
		u64 s_sequence_errors = 0;

		std::optional<std::string_view> Mode()
		{
			const char* const configured = std::getenv(kModeEnvironment.data());
			if (!configured)
				return std::nullopt;
			const std::string_view mode(configured);
			return mode == "oracle" || mode == "native" ? std::optional<std::string_view>(mode) : std::nullopt;
		}

		bool HasMissionTarget()
		{
			const char* const configured = std::getenv(kTargetEnvironment.data());
			return configured && std::string_view(configured) == kMissionTarget;
		}

		bool IsTargetRecognized()
		{
			return VMManager::GetDiscSerial() == kTargetSerial && VMManager::GetDiscCRC() == kTargetCrc;
		}

		bool IsEnabled()
		{
			return IsSurfacelessControlTest() && IsTargetRecognized() && HasMissionTarget() && Mode().has_value();
		}

		bool IsObservedPc(const u32 pc)
		{
			return pc == kShellLoadLevelPc || pc == kShellLoadLevelReturnPc;
		}

		bool ValidateMissionEntry()
		{
			const u32 shell = cpuRegs.GPR.n.a0.UL[0];
			const u32 path = cpuRegs.GPR.n.a1.UL[0];
			u32 singleton = 0;
			if (shell == 0 || path != shell || !GuestObjects::ReadWord(kShellSingletonAddress, &singleton) ||
				singleton != shell)
			{
				return false;
			}

			std::array<char, kMissionPath.size() + 1> value{};
			return GuestObjects::ReadBytes(path, value.data(), static_cast<u32>(value.size())) && value.back() == '\0' &&
			       std::string_view(value.data(), value.size() - 1) == kMissionPath;
		}

		void AppendPoint(std::string& body, const std::string_view kind, const u32 pc,
			const LoadTimingPoint::Point& point)
		{
			body += "{\"kind\":\"" + std::string(kind) + "\",\"path\":\"" + std::string(kMissionPath) + "\"";
			body += ",\"pc\":" + std::to_string(pc);
			body += ",\"ordinal\":" + std::to_string(point.ordinal);
			body += ",\"ee_cycle\":" + std::to_string(point.ee_cycle);
			body += ",\"iop_cycle\":" + std::to_string(point.iop_cycle);
			body += ",\"frame\":" + std::to_string(point.frame);
			body += ",\"host_time_ns\":" + std::to_string(point.host_time_ns) + '}';
		}
	} // namespace

	bool ShouldInstrumentEePc(const u32 pc)
	{
		// The recompiler can see MainLoop blocks before VMManager publishes the
		// final disc identity. Split configured timing PCs from process start;
		// ObserveEeExecution performs the exact live title/control-test gate.
		return IsObservedPc(pc) && HasMissionTarget() && Mode().has_value();
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (!IsObservedPc(pc) || !IsEnabled())
			return;

		std::lock_guard lock(s_mutex);
		if (pc == kShellLoadLevelPc)
		{
			if (!ValidateMissionEntry())
				return;
			if (s_start || s_end)
			{
				++s_sequence_errors;
				return;
			}
			s_start = LoadTimingPoint::CaptureNext(s_ordinal);
			return;
		}

		if (!s_start)
			return;
		if (s_end)
		{
			++s_sequence_errors;
			return;
		}
		s_end = LoadTimingPoint::CaptureNext(s_ordinal);
	}

	std::string SnapshotJson()
	{
		const std::optional<std::string_view> mode = Mode();
		const bool byte_trace_disabled = std::getenv("AVPE_ASSET_BYTE_TRACE") == nullptr;
		std::lock_guard lock(s_mutex);
		const bool complete = mode && byte_trace_disabled && s_start && s_end && s_sequence_errors == 0 &&
		                      s_end->ee_cycle > s_start->ee_cycle && s_end->iop_cycle > s_start->iop_cycle &&
		                      s_end->frame >= s_start->frame && s_end->host_time_ns > s_start->host_time_ns;
		std::string body = "{\"schema\":\"" + std::string(kSchema) + "\",\"target\":\"mission\",\"enabled\":";
		body += IsEnabled() ? "true" : "false";
		body += ",\"target_recognized\":";
		body += IsTargetRecognized() ? "true" : "false";
		body += ",\"byte_trace_disabled\":";
		body += byte_trace_disabled ? "true" : "false";
		body += ",\"mode\":\"" + std::string(mode.value_or("disabled")) + "\"";
		body += ",\"complete\":";
		body += complete ? "true" : "false";
		body += ",\"sequence_errors\":" + std::to_string(s_sequence_errors);
		body += ",\"start\":";
		if (s_start)
			AppendPoint(body, "shell-load-level-entry", kShellLoadLevelPc, *s_start);
		else
			body += "null";
		body += ",\"end\":";
		if (s_end)
			AppendPoint(body, "shell-load-level-return", kShellLoadLevelReturnPc, *s_end);
		else
			body += "null";
		body += ",\"deltas\":";
		if (complete)
		{
			body += "{\"ee_cycles\":" + std::to_string(s_end->ee_cycle - s_start->ee_cycle);
			body += ",\"iop_cycles\":" + std::to_string(s_end->iop_cycle - s_start->iop_cycle);
			body += ",\"frames\":" + std::to_string(s_end->frame - s_start->frame);
			body += ",\"host_elapsed_ns\":" +
			        std::to_string(s_end->host_time_ns - s_start->host_time_ns) + '}';
		}
		else
		{
			body += "null";
		}
		body += '}';
		return body;
	}

	void Reset()
	{
		std::lock_guard lock(s_mutex);
		s_start.reset();
		s_end.reset();
		s_ordinal = 0;
		s_sequence_errors = 0;
	}
} // namespace AVPE::NativeMissionLoadTiming
