// The in-game order card as a PC command card. Fork-local.

#pragma once

#include "AVPE/NativeInputCallbacks.h"

#include <functional>
#include <mutex>

namespace AVPE
{
	// The order card (Aggressive, Patrol, Waypoint, ...) is a menu the pad shows only
	// while R2 is held, and it replaces the unit's menu while shown. A PC command key
	// opens it through the R2 button's own callback, fires the order its label names
	// and closes it again, so the unit's menu, control groups and abilities stay live.
	// Each stage is one GInputDevice callback; the next waits until it has run.
	class NativeCommandCard final
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

		struct Guest
		{
			NativeInputCallbacks::Access read;
			std::function<bool()> dispatch_idle;
			std::function<bool(const NativeInputCallbacks::Target&)> queue;
		};

		// Host thread. Each returns false while an earlier request is still running.
		bool Command(char letter);
		bool Show(bool shown);

		// EE thread, at InputProcessPc.
		void Step(u32 input_device, const Guest& guest);
		void Reset();

		static NativeCommandCard& Process();
		static Guest LiveGuest();

	private:
		enum class Stage : u8
		{
			Idle,
			Open,
			Fire,
			Close,
		};

		Stage Advance(u32 input_device, const Guest& guest);
		Stage Fire(u32 entries, u32 count, u32 in_game_menu, const Guest& guest);
		Stage Finish() const;

		std::mutex m_mutex;
		Stage m_stage = Stage::Idle;
		char m_letter = 0;
		// The card closes after the order when this request opened it or Show(false) came meanwhile.
		bool m_opened = false;
		bool m_hide = false;
	};
} // namespace AVPE
