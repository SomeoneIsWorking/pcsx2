// AVP:E game-save BIOS/IOP capture boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeGameSaveBoundary.h"

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

namespace AVPE::NativeGameSaveBoundary
{
	namespace
	{
		constexpr std::string_view TargetSerial = "SLUS-20147";
		constexpr u32 TargetCrc = 0x64DA78A3;
		constexpr u32 ProfileSingletonAddress = 0x0036703C;
		constexpr u32 SaveGameEntryPc = 0x00130170;
		// The final two instructions are `jr ra; addiu sp, sp, 0xAF0`.
		// Sampling at the jump preserves the completed operation's v0 result.
		constexpr u32 SaveGameReturnPc = 0x00130374;

		std::mutex s_mutex;
		std::condition_variable s_completion;
		std::atomic_bool s_armed{false};
		std::optional<LoadTimingPoint::Point> s_entry;
		std::optional<LoadTimingPoint::Point> s_return;
		s32 s_result = 0;
		u64 s_ordinal = 0;
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
			result += ",\"game_save_boundary\":{\"entry_pc\":" + std::to_string(SaveGameEntryPc);
			result += ",\"return_pc\":" + std::to_string(SaveGameReturnPc);
			result += ",\"complete\":" + std::string(complete ? "true" : "false");
			result += ",\"succeeded\":" + std::string(complete && s_result == 0 ? "true" : "false");
			result += ",\"result\":" + std::to_string(s_result);
			result += ",\"sequence_errors\":" + std::to_string(s_sequence_errors);
			result += ",\"entry\":";
			if (s_entry)
				AppendPoint(result, *s_entry, SaveGameEntryPc);
			else
				result += "null";
			result += ",\"return\":";
			if (s_return)
				AppendPoint(result, *s_return, SaveGameReturnPc);
			else
				result += "null";
			result += "}}";
			return result;
		}
	} // namespace

	bool Start()
	{
		if (!IsSupportedTarget())
			return false;
		// Control-test startup enables the general census. A game-save probe
		// must discard that pre-selection traffic until the exact title entry.
		NativeBiosTrace::SetEnabled(false);
		std::lock_guard lock(s_mutex);
		s_entry.reset();
		s_return.reset();
		s_result = 0;
		s_ordinal = 0;
		s_sequence_errors = 0;
		s_capture.clear();
		s_armed.store(true, std::memory_order_release);
		return true;
	}

	bool ShouldInstrumentEePc(const u32 pc)
	{
		// This runs while the recompiler translates blocks, before a control
		// request can arm the boundary. Runtime admission happens in Observe.
		return pc == SaveGameEntryPc || pc == SaveGameReturnPc;
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (!s_armed.load(std::memory_order_acquire) || !IsSupportedTarget())
			return;
		std::lock_guard lock(s_mutex);
		if (!s_armed.load(std::memory_order_relaxed))
			return;
		if (pc == SaveGameEntryPc)
		{
			if (!IsCurrentProfile())
				return;
			if (s_entry)
			{
				++s_sequence_errors;
				return;
			}
			// Capture only the title-owned save operation, not the UI action that
			// selected its slot.
			NativeBiosTrace::Reset();
			NativeBiosTrace::SetEnabled(true);
			s_entry = LoadTimingPoint::CaptureNext(s_ordinal);
			return;
		}
		if (pc != SaveGameReturnPc || !s_entry || s_return)
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
} // namespace AVPE::NativeGameSaveBoundary
