// AVP:E title-to-profile lifecycle observation. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeTitleTransition.h"

#include "AVPE/AVPE.h"
#include "AVPE/GuestObjects.h"
#include "R5900.h"
#include "VMManager.h"

#include <atomic>
#include <string>
#include <string_view>

namespace AVPE::NativeTitleTransition
{
	namespace
	{
		constexpr std::string_view TargetSerial = "SLUS-20147";
		constexpr u32 TargetCrc = 0x64DA78A3;
		constexpr u32 PressStartItemActivatedPc = 0x00209F30;
		constexpr u32 ProfileMenuCreatePc = 0x002075F0;
		constexpr u32 PressStartMenuVtable = 0x00342A50;
		constexpr u32 LoadMenuKillMeAction = 0x807F1E5F;
		constexpr u32 MenuItemActionOffset = 0x110;

		std::atomic_bool s_armed{false};
		std::atomic<u64> s_next_ordinal{1};
		std::atomic<u64> s_press_start_ordinal{0};
		std::atomic<u64> s_profile_create_ordinal{0};
		std::atomic<u64> s_invalid_press_start_entries{0};
		std::atomic<u64> s_unexpected_profile_create_entries{0};
		std::atomic<u32> s_press_start_menu{0};
		std::atomic<u32> s_press_start_item_action{0};
		std::atomic<u32> s_profile_create_parent{0};

		bool IsSupportedTarget()
		{
			return IsSurfacelessControlTest() && VMManager::GetDiscSerial() == TargetSerial &&
			       VMManager::GetDiscCRC() == TargetCrc;
		}

		void RecordPressStartEntry()
		{
			const u32 menu = cpuRegs.GPR.n.a0.UL[0];
			const u32 item = cpuRegs.GPR.n.a1.UL[0];
			u32 vtable = 0;
			u32 action = 0;
			if (!GuestObjects::IsPlausibleObject(menu) || !GuestObjects::IsPlausibleObject(item) ||
				!GuestObjects::ReadWord(menu, &vtable) ||
				!GuestObjects::ReadWord(item + MenuItemActionOffset, &action) ||
				vtable != PressStartMenuVtable || action != LoadMenuKillMeAction)
			{
				s_invalid_press_start_entries.fetch_add(1, std::memory_order_release);
				return;
			}
			if (s_press_start_ordinal.load(std::memory_order_acquire) != 0)
			{
				s_invalid_press_start_entries.fetch_add(1, std::memory_order_release);
				return;
			}
			s_press_start_menu.store(menu, std::memory_order_release);
			s_press_start_item_action.store(action, std::memory_order_release);
			s_press_start_ordinal.store(s_next_ordinal.fetch_add(1, std::memory_order_relaxed),
				std::memory_order_release);
		}

		void RecordProfileCreateEntry()
		{
			if (s_press_start_ordinal.load(std::memory_order_acquire) == 0 ||
				s_profile_create_ordinal.load(std::memory_order_acquire) != 0)
			{
				s_unexpected_profile_create_entries.fetch_add(1, std::memory_order_release);
				return;
			}
			s_profile_create_parent.store(cpuRegs.GPR.n.a0.UL[0], std::memory_order_release);
			s_profile_create_ordinal.store(s_next_ordinal.fetch_add(1, std::memory_order_relaxed),
				std::memory_order_release);
			s_armed.store(false, std::memory_order_release);
		}
	} // namespace

	bool Start()
	{
		if (!IsSupportedTarget())
			return false;
		Reset();
		s_armed.store(true, std::memory_order_release);
		return true;
	}

	void Reset()
	{
		s_armed.store(false, std::memory_order_release);
		s_next_ordinal.store(1, std::memory_order_release);
		s_press_start_ordinal.store(0, std::memory_order_release);
		s_profile_create_ordinal.store(0, std::memory_order_release);
		s_invalid_press_start_entries.store(0, std::memory_order_release);
		s_unexpected_profile_create_entries.store(0, std::memory_order_release);
		s_press_start_menu.store(0, std::memory_order_release);
		s_press_start_item_action.store(0, std::memory_order_release);
		s_profile_create_parent.store(0, std::memory_order_release);
	}

	bool ShouldInstrumentEePc(const u32 pc)
	{
		return pc == PressStartItemActivatedPc || pc == ProfileMenuCreatePc;
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (!s_armed.load(std::memory_order_acquire) || !IsSupportedTarget())
			return;
		if (pc == PressStartItemActivatedPc)
			RecordPressStartEntry();
		else if (pc == ProfileMenuCreatePc)
			RecordProfileCreateEntry();
	}

	std::string SnapshotJson()
	{
		const u64 press_start_ordinal = s_press_start_ordinal.load(std::memory_order_acquire);
		const u64 profile_create_ordinal = s_profile_create_ordinal.load(std::memory_order_acquire);
		const bool complete = press_start_ordinal != 0 && profile_create_ordinal > press_start_ordinal &&
		                      s_invalid_press_start_entries.load(std::memory_order_acquire) == 0 &&
		                      s_unexpected_profile_create_entries.load(std::memory_order_acquire) == 0;
		return "{\"schema\":\"avpe-title-transition-v1\",\"armed\":" +
		       std::string(s_armed.load(std::memory_order_acquire) ? "true" : "false") +
		       ",\"complete\":" + std::string(complete ? "true" : "false") +
		       ",\"press_start\":{\"pc\":" + std::to_string(PressStartItemActivatedPc) +
		       ",\"ordinal\":" + std::to_string(press_start_ordinal) +
		       ",\"menu\":" + std::to_string(s_press_start_menu.load(std::memory_order_acquire)) +
		       ",\"item_action\":" + std::to_string(s_press_start_item_action.load(std::memory_order_acquire)) +
		       ",\"invalid_entries\":" +
		       std::to_string(s_invalid_press_start_entries.load(std::memory_order_acquire)) + "},\"profile_create\":{\"pc\":" +
		       std::to_string(ProfileMenuCreatePc) + ",\"ordinal\":" + std::to_string(profile_create_ordinal) +
		       ",\"parent\":" + std::to_string(s_profile_create_parent.load(std::memory_order_acquire)) +
		       ",\"unexpected_entries\":" +
		       std::to_string(s_unexpected_profile_create_entries.load(std::memory_order_acquire)) + "}}";
	}
} // namespace AVPE::NativeTitleTransition
