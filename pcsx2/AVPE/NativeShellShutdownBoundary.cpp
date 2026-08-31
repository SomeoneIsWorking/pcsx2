// AVP:E shell-shutdown BIOS/IOP capture boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeShellShutdownBoundary.h"

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

namespace AVPE::NativeShellShutdownBoundary
{
	namespace
	{
		constexpr std::string_view TargetSerial = "SLUS-20147";
		constexpr u32 TargetCrc = 0x64DA78A3;
		constexpr u32 ShellSingletonAddress = 0x003672F0;
		constexpr u32 QuitEntryPc = 0x0016F8D0;
		constexpr u32 MainLoopReturnPc = 0x0016F8C8;
		constexpr u32 ShellRequestOffset = 0x808;
		constexpr u32 QuitRequestBit = 4;

		std::mutex s_mutex;
		std::condition_variable s_completion;
		std::atomic_bool s_armed{false};
		std::optional<LoadTimingPoint::Point> s_quit_entry;
		std::optional<LoadTimingPoint::Point> s_main_loop_return;
		u64 s_ordinal = 0;
		u32 s_shell = 0;
		u32 s_sequence_errors = 0;
		bool s_quit_bit_observed = false;
		std::string s_capture;

		bool IsSupportedTarget()
		{
			return IsSurfacelessControlTest() && VMManager::GetDiscSerial() == TargetSerial &&
			       VMManager::GetDiscCRC() == TargetCrc;
		}

		bool ReadCurrentShell(u32* shell)
		{
			return GuestObjects::ReadWord(ShellSingletonAddress, shell) &&
			       GuestObjects::IsPlausibleObject(*shell);
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
			const bool complete = s_quit_entry && s_main_loop_return && s_quit_bit_observed &&
			                      s_sequence_errors == 0 &&
			                      s_main_loop_return->ee_cycle > s_quit_entry->ee_cycle &&
			                      s_main_loop_return->iop_cycle >= s_quit_entry->iop_cycle &&
			                      s_main_loop_return->frame >= s_quit_entry->frame &&
			                      s_main_loop_return->host_time_ns > s_quit_entry->host_time_ns;
			result += ",\"shell_shutdown_boundary\":{\"quit_entry_pc\":" + std::to_string(QuitEntryPc);
			result += ",\"main_loop_return_pc\":" + std::to_string(MainLoopReturnPc);
			result += ",\"shell\":" + std::to_string(s_shell);
			result += ",\"quit_bit_observed\":" + std::string(s_quit_bit_observed ? "true" : "false");
			result += ",\"complete\":" + std::string(complete ? "true" : "false");
			result += ",\"sequence_errors\":" + std::to_string(s_sequence_errors);
			result += ",\"quit_entry\":";
			if (s_quit_entry)
				AppendPoint(result, *s_quit_entry, QuitEntryPc);
			else
				result += "null";
			result += ",\"main_loop_return\":";
			if (s_main_loop_return)
				AppendPoint(result, *s_main_loop_return, MainLoopReturnPc);
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
		u32 shell = 0;
		if (!ReadCurrentShell(&shell))
			return false;

		// Exclude setup and menu-navigation activity until the exact title-owned
		// shutdown request enters CShell.
		NativeBiosTrace::SetEnabled(false);
		std::lock_guard lock(s_mutex);
		s_quit_entry.reset();
		s_main_loop_return.reset();
		s_ordinal = 0;
		s_shell = shell;
		s_sequence_errors = 0;
		s_quit_bit_observed = false;
		s_capture.clear();
		s_armed.store(true, std::memory_order_release);
		return true;
	}

	bool ShouldInstrumentEePc(const u32 pc)
	{
		// This runs while the recompiler translates blocks, before a control
		// request can arm the title-specific boundary. Runtime admission happens
		// in ObserveEeExecution.
		return pc == QuitEntryPc || pc == MainLoopReturnPc;
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (!s_armed.load(std::memory_order_acquire) || !IsSupportedTarget())
			return;
		std::lock_guard lock(s_mutex);
		if (!s_armed.load(std::memory_order_relaxed))
			return;

		if (pc == QuitEntryPc)
		{
			if (cpuRegs.GPR.n.a0.UL[0] != s_shell)
				return;
			if (s_quit_entry)
			{
				++s_sequence_errors;
				return;
			}
			NativeBiosTrace::Reset();
			NativeBiosTrace::SetEnabled(true);
			s_quit_entry = LoadTimingPoint::CaptureNext(s_ordinal);
			return;
		}

		if (pc != MainLoopReturnPc || !s_quit_entry || s_main_loop_return)
			return;
		u32 request = 0;
		if (!GuestObjects::ReadWord(s_shell + ShellRequestOffset, &request) ||
			(request & QuitRequestBit) == 0)
			++s_sequence_errors;
		else
			s_quit_bit_observed = true;
		s_main_loop_return = LoadTimingPoint::CaptureNext(s_ordinal);
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
} // namespace AVPE::NativeShellShutdownBoundary
