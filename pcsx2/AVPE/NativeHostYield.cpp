// AVP:E title-owned host-work yield boundaries. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeHostYield.h"

#include "Host.h"
#include "VMManager.h"

#include <atomic>

namespace AVPE::NativeHostYield
{
	namespace
	{
		constexpr u32 MissionGoalsWaitLoopPc = 0x002052C8;
		constexpr const char* TargetSerial = "SLUS-20147";
		constexpr u32 TargetCrc = 0x64DA78A3;
		std::atomic<u32> s_pending_requests{0};
	} // namespace

	void Request()
	{
		s_pending_requests.fetch_add(1, std::memory_order_release);
	}

	void CompleteRequest()
	{
		s_pending_requests.fetch_sub(1, std::memory_order_release);
	}

	bool ShouldInstrumentEePc(const u32 pc)
	{
		return pc == MissionGoalsWaitLoopPc;
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (pc != MissionGoalsWaitLoopPc || s_pending_requests.load(std::memory_order_acquire) == 0 ||
			VMManager::GetDiscSerial() != TargetSerial || VMManager::GetDiscCRC() != TargetCrc)
		{
			return;
		}

		// This title-owned synchronous wait does not reach PCSX2's ordinary frame
		// input poll. Keep pumping while a blocking host transaction is pending;
		// the CPU-thread operation clears the request after it executes.
		Host::PumpMessagesOnCPUThread();
	}
} // namespace AVPE::NativeHostYield
