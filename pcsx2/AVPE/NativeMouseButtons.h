// The guest calls each mouse edge makes. Fork-local.

#pragma once

#include "AVPE/EECallShuttle.h"
#include "AVPE/NativeInput.h"
#include "AVPE/NativeInputCallbacks.h"

#include <array>
#include <vector>

namespace AVPE
{
	// Left is the pointer's own mouse handlers; Shift and Ctrl change only how the release
	// selects. Right is the pad's Circle on the DWIM button the game registers for what the
	// cursor is over: GDwimMenuButton (attack, pick up, enter, ...) or GMoveDwimMenuButton on
	// open ground. Selection and unit sounds wait on SIF, so the calls must run deferred.
	class NativeMouseButtons final
	{
	public:
		static inline constexpr u32 PressMousePrimaryFunction = 0x001B52C0;
		static inline constexpr u32 ReleaseMousePrimaryFunction = 0x001B52D0;
		static inline constexpr u32 SelectChangingFunction = 0x001B26A0;
		static inline constexpr u32 DoubleClickSelectChangingFunction = 0x001B2790;
		static inline constexpr u32 InGameMenuRefreshFunction = 0x00279670;
		static inline constexpr u32 InGameMenuSingleton = 0x003687FC;
		static inline constexpr u32 DwimButtonVtable = 0x0035B9A0;
		static inline constexpr u32 MoveDwimButtonVtable = 0x0035B8A0;
		static inline constexpr u32 MoveDwimFocusKeyActivate = 0x0027C530;
		static inline constexpr u32 MoveDwimHotKeyActivate = 0x0027C3A0;

		struct Frame
		{
			u32 pointer = 0;
			u32 in_game_menu = 0;
			NativeInputCallbacks::Registry registry;
		};

		// The calls this edge makes, in order; empty when it does nothing.
		std::vector<EECallShuttle::Request> Calls(NativeInput::MouseButton button, NativeInput::ButtonEdge edge,
			NativeInput::SelectionMode mode, const Frame& frame, const NativeInputCallbacks::Access& read);
		void Reset();

		static NativeMouseButtons& Process();

	private:
		struct ContextButton
		{
			u32 vtable = 0;
			u32 focus = 0;
			u32 hot = 0;
		};

		static constexpr std::array<ContextButton, 2> ContextButtons{{
			{DwimButtonVtable, NativeInputCallbacks::MenuItemFocusKeyActivate, NativeInputCallbacks::MenuItemHotKeyActivate},
			{MoveDwimButtonVtable, MoveDwimFocusKeyActivate, MoveDwimHotKeyActivate},
		}};

		// The context button whose focus key the last right press ran; its hotkey ends the click.
		const ContextButton* m_focused = nullptr;
	};
} // namespace AVPE
