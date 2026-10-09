// The right mouse button as the pad's context button. Fork-local.

#pragma once

#include "AVPE/NativeInputDispatch.h"

#include <array>
#include <mutex>

namespace AVPE
{
	// The game registers one DWIM button for what the cursor is over: GDwimMenuButton for a
	// target (attack, pick up, enter, ...) or GMoveDwimMenuButton for open ground. The pad's
	// Circle press runs its focus key and the release its hotkey; the right button runs the
	// same callbacks through the ordinary GInputDevice dispatch, where their SIF audio waits complete.
	class NativeContextAction final
	{
	public:
		static inline constexpr u32 DwimButtonVtable = 0x0035B9A0;
		static inline constexpr u32 MoveDwimButtonVtable = 0x0035B8A0;
		static inline constexpr u32 MoveDwimFocusKeyActivate = 0x0027C530;
		static inline constexpr u32 MoveDwimHotKeyActivate = 0x0027C3A0;

		using Guest = NativeInputDispatch::CallbackQueue;

		// Host thread. False when too many edges are pending.
		bool Press();
		bool Release();

		// EE thread, at GInputDevice::Process.
		void Step(u32 input_device, const Guest& guest);
		void Reset();

		static NativeContextAction& Process();

	private:
		struct Button
		{
			u32 vtable = 0;
			u32 focus = 0;
			u32 hot = 0;
		};

		static constexpr std::array<Button, 2> Buttons{{
			{DwimButtonVtable, NativeInputCallbacks::MenuItemFocusKeyActivate, NativeInputCallbacks::MenuItemHotKeyActivate},
			{MoveDwimButtonVtable, MoveDwimFocusKeyActivate, MoveDwimHotKeyActivate},
		}};
		static constexpr size_t MaxEdges = 4;

		bool Push(bool press);

		std::mutex m_mutex;
		std::array<bool, MaxEdges> m_edges{};
		size_t m_count = 0;
		// The button whose focus key the last press ran; its hotkey ends the click.
		const Button* m_focused = nullptr;
	};
} // namespace AVPE
