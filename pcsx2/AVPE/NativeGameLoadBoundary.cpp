// AVP:E game-load BIOS/IOP capture boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeGameLoadBoundary.h"

#include "AVPE/AVPE.h"
#include "AVPE/GuestObjects.h"
#include "AVPE/LoadTimingPoint.h"
#include "AVPE/NativeBiosTrace.h"
#include "R5900.h"
#include "VMManager.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string_view>

namespace AVPE::NativeGameLoadBoundary
{
	namespace
	{
		constexpr std::string_view TargetSerial = "SLUS-20147";
		constexpr u32 TargetCrc = 0x64DA78A3;
		constexpr u32 ProfileSingletonAddress = 0x0036703C;
		constexpr u32 LoadPacifyProcessPc = 0x00202C20;
		constexpr u32 LoadGameEntryPc = 0x00130000;
		// The function ends with `jr ra; addiu sp, sp, 0xBB0`.
		constexpr u32 LoadGameReturnPc = 0x00130168;

		std::mutex s_mutex;
		std::condition_variable s_completion;
		std::atomic_bool s_armed{false};
		std::optional<LoadTimingPoint::Point> s_entry;
		std::optional<LoadTimingPoint::Point> s_return;
		s32 s_result = 0;
		u64 s_ordinal = 0;
		u32 s_pacify_process_calls = 0;
		u32 s_sequence_errors = 0;
		std::string s_capture;

		bool IsSupportedTarget()
		{
			return IsSurfacelessControlTest() && VMManager::GetDiscSerial() == TargetSerial &&
			       VMManager::GetDiscCRC() == TargetCrc;
		}

		bool IsCurrentProfile()
		{
			u32 profile = 0;
			return GuestObjects::ReadWord(ProfileSingletonAddress, &profile) && profile != 0 &&
			       cpuRegs.GPR.n.a0.UL[0] == profile;
		}

		void AppendPoint(std::string& json, const LoadTimingPoint::Point& point, const u32 pc)
		{
			json += "{\"pc\":" + std::to_string(pc);
			json += ",\"ordinal\":" + std::to_string(point.ordinal);
			json += ",\"ee_cycle\":" + std::to_string(point.ee_cycle);
			json += ",\"iop_cycle\":" + std::to_string(point.iop_cycle);
			json += ",\"frame\":" + std::to_string(point.frame);
			json += ",\"host_time_ns\":" + std::to_string(point.host_time_ns) + '}';
		}

		std::string BuildResult(const std::string& trace)
		{
			std::string result = trace;
			if (result.empty() || result.back() != '}')
				result = "{}";
			result.pop_back();
			const bool complete = s_entry && s_return && s_sequence_errors == 0 &&
			                      s_return->ee_cycle > s_entry->ee_cycle &&
			                      s_return->iop_cycle >= s_entry->iop_cycle &&
			                      s_return->frame >= s_entry->frame &&
			                      s_return->host_time_ns > s_entry->host_time_ns;
			result += ",\"game_load_boundary\":{\"entry_pc\":" + std::to_string(LoadGameEntryPc);
			result += ",\"return_pc\":" + std::to_string(LoadGameReturnPc);
			result += ",\"pacify_process_pc\":" + std::to_string(LoadPacifyProcessPc);
			result += ",\"pacify_process_calls\":" + std::to_string(s_pacify_process_calls);
			result += ",\"complete\":" + std::string(complete ? "true" : "false");
			result += ",\"succeeded\":" + std::string(complete && s_result == 0 ? "true" : "false");
			result += ",\"result\":" + std::to_string(s_result);
			result += ",\"sequence_errors\":" + std::to_string(s_sequence_errors);
			result += ",\"entry\":";
			if (s_entry)
				AppendPoint(result, *s_entry, LoadGameEntryPc);
			else
				result += "null";
			result += ",\"return\":";
			if (s_return)
				AppendPoint(result, *s_return, LoadGameReturnPc);
			else
				result += "null";
			return result + "}}";
		}
	} // namespace

	bool Start()
	{
		if (!IsSupportedTarget())
			return false;
		NativeBiosTrace::SetEnabled(false);
		std::lock_guard lock(s_mutex);
		s_entry.reset();
		s_return.reset();
		s_result = 0;
		s_ordinal = 0;
		s_pacify_process_calls = 0;
		s_sequence_errors = 0;
		s_capture.clear();
		s_armed.store(true, std::memory_order_release);
		return true;
	}

	bool ShouldInstrumentEePc(const u32 pc)
	{
		return pc == LoadPacifyProcessPc || pc == LoadGameEntryPc || pc == LoadGameReturnPc;
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (!s_armed.load(std::memory_order_acquire) || !IsSupportedTarget())
			return;
		std::lock_guard lock(s_mutex);
		if (!s_armed.load(std::memory_order_relaxed))
			return;
		if (pc == LoadPacifyProcessPc)
		{
			++s_pacify_process_calls;
			return;
		}
		if (pc == LoadGameEntryPc)
		{
			if (!IsCurrentProfile())
				return;
			if (s_entry)
			{
				++s_sequence_errors;
				return;
			}
			NativeBiosTrace::Reset();
			NativeBiosTrace::SetEnabled(true);
			s_entry = LoadTimingPoint::CaptureNext(s_ordinal);
			return;
		}
		if (pc != LoadGameReturnPc || !s_entry || s_return)
			return;
		s_return = LoadTimingPoint::CaptureNext(s_ordinal);
		s_result = cpuRegs.GPR.n.v0.SL[0];
		s_armed.store(false, std::memory_order_release);
		s_capture = BuildResult(NativeBiosTrace::SnapshotAndDisableJson());
		s_completion.notify_all();
	}

	std::string CaptureJson(const std::chrono::milliseconds timeout)
	{
		std::unique_lock lock(s_mutex);
		if (s_capture.empty() && !s_completion.wait_for(lock, timeout, []() { return !s_capture.empty(); }))
		{
			s_armed.store(false, std::memory_order_release);
			s_capture = BuildResult(NativeBiosTrace::SnapshotAndDisableJson());
		}
		return s_capture;
	}
} // namespace AVPE::NativeGameLoadBoundary
