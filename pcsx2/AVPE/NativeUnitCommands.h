// PC keys for in-mission unit commands. Fork-local.

#pragma once

#include "AVPE/NativeInputCallbacks.h"

#include <array>
#include <functional>
#include <mutex>

namespace AVPE
{
	// Runs a PC key's unit command as a short sequence of the guest's own registered
	// GInputDevice callbacks, one per frame, each after the previous one has run.
	// The order card (Aggressive, Patrol, Waypoint, ...) shows only while the pad holds
	// R2 and replaces the unit's menu while shown, so an order letter opens it through
	// the R2 button's callback, fires the order and closes it again.
	class NativeUnitCommands final
	{
	public:
		// GInputDevice::Process entry; $a0 is the device, before this frame's dispatch.
		static inline constexpr u32 InputProcessPc = 0x00114490;
		static inline constexpr u32 CallbackArrayOffset = 0x48;
		static inline constexpr u32 InGameMenuPointer = 0x003687FC;
		static inline constexpr u32 CurrentMenuOffset = 0x27C;
		// GToggleMenuButton sets this byte of GInGameMenu while its menu is shown.
		static inline constexpr u32 CardShownOffset = 0x293;
		static inline constexpr u32 PointerInstance = 0x00367720;
		// GAvPPointer's selection ZArray; its count is the second word.
		static inline constexpr u32 SelectionOffset = 0x1B0;
		static inline constexpr u32 ToggleMenuButtonVtable = 0x0035B3A0;
		// GToggleMenuButton::FocusKeyActivate (R2 press) and HotKeyActivate (R2 release).
		static inline constexpr u32 ToggleOpenFunction = 0x0027D1F0;
		static inline constexpr u32 ToggleCloseFunction = 0x0027D1B0;
		// L2: while held, a group item creates the group instead of selecting it.
		static inline constexpr u32 GroupingButtonVtable = 0x0035B6A0;
		static inline constexpr u32 GroupingPressFunction = 0x00284840;
		static inline constexpr u32 GroupingReleaseFunction = 0x00284820;
		// The unit menu's items by pad event, GetCRC of the event name: d-pad up, right,
		// down and left are groups 1-4 (GAvPMenu::ItemActivated SelectGroup/CreateGroup).
		static inline constexpr std::array<u32, 4> GroupHotkeys{0x9376DABF, 0xAC71B3CF, 0x2A13E8FB, 0x5139E023};
		// Right_TopClusterButton_Release (Triangle): GMiniMap::GoToBattleEvent.
		static inline constexpr u32 EventHotkey = 0x17F3FBBE;
		// Right_LeftClusterButton_Release (Square): GAvPMenu::JumpToBase.
		static inline constexpr u32 BaseHotkey = 0x26BA5594;

		struct Guest
		{
			NativeInputCallbacks::Access read;
			std::function<bool()> dispatch_idle;
			std::function<bool(const NativeInputCallbacks::Target&)> queue;
		};

		// Host thread. Each returns false while an earlier command is still running.
		bool CardOrder(char letter);
		bool ShowCard(bool shown);
		// group is 0..3.
		bool RecallGroup(u32 group);
		bool AssignGroup(u32 group);
		bool JumpToEvent();
		bool JumpToBase();

		// EE thread, at InputProcessPc.
		void Step(u32 input_device, const Guest& guest);
		void Reset();

		static NativeUnitCommands& Process();
		static Guest LiveGuest();

	private:
		enum class Action : u8
		{
			OpenCard,
			FireCardOrder,
			CloseCard,
			PressGrouping,
			FireMenuItem,
			ReleaseGrouping,
		};

		struct Frame
		{
			u32 entries = 0;
			u32 count = 0;
			u32 in_game_menu = 0;
			bool card_shown = false;
		};

		static constexpr size_t MaxActions = 3;

		bool Begin(std::initializer_list<Action> actions, char letter, u32 hotkey = 0);
		// False when the rest of the sequence must not run.
		bool Run(Action action, const Frame& frame, const Guest& guest, bool* queued);

		std::mutex m_mutex;
		std::array<Action, MaxActions> m_actions{};
		size_t m_count = 0;
		size_t m_next = 0;
		char m_letter = 0;
		u32 m_hotkey = 0;
		// The card closes after the order when this command opened it or ShowCard(false) came meanwhile.
		bool m_opened = false;
		bool m_hide = false;
	};
} // namespace AVPE
