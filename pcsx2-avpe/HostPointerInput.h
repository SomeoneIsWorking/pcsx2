// AVPE product-host mouse policy. Fork-local; not for upstream PCSX2.
#pragma once

#include "AVPE/NativeCameraInput.h"
#include "AVPE/NativeInput.h"
#include "AVPE/NativeMenuInput.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_set>

namespace AVPE
{
	// Routes mouse motion, buttons and the wheel to the live AVP:E menu when one owns
	// navigation, otherwise to the mission pointer and camera.
	class HostPointerInput final
	{
	public:
		enum class Button : std::uint8_t
		{
			Primary,
			Secondary,
		};

		struct Guest
		{
			std::function<NativeMenuInput::Result()> inspect_menu;
			std::function<NativeMenuInput::PointerResult(float, float)> move_menu_pointer;
			std::function<NativeMenuInput::PointerResult()> activate_menu_pointer;
			std::function<NativeInput::Result(float, float)> move_pointer;
			std::function<NativeInput::ButtonResult(NativeInput::MouseButton, NativeInput::ButtonEdge)> button_edge;
			std::function<NativeCameraInput::Result(NativeCameraInput::Action, float, float)> camera;

			static Guest Live();
		};

		explicit HostPointerInput(Guest guest = Guest::Live());

		// Coordinates are normalized to the presented guest image.
		bool Move(float normalized_x, float normalized_y);
		bool Press(Button button);
		bool Release(Button button);
		bool DoubleClick(Button button);
		bool Wheel(float steps);
		// The cursor left the window; edge scrolling stops until it returns.
		void Leave();
		// Each input tick: a mission cursor at the screen edge scrolls the camera.
		void Tick();

		// Fraction of the presented image at each edge that scrolls.
		static inline constexpr float EdgeBand = 0.02f;

	private:
		// True when the pointer is on the minimap and the camera jumped there.
		bool JumpToMinimapPointer();

		struct Position
		{
			float x;
			float y;
		};

		Guest m_guest;
		// The last mission cursor position; empty in menus and outside the window.
		std::optional<Position> m_mission_position;
		std::unordered_set<Button> m_menu_buttons;
		std::unordered_set<Button> m_gameplay_buttons;
		// Held on the minimap: the camera follows the pointer instead of selecting.
		std::unordered_set<Button> m_minimap_buttons;
		std::unordered_set<Button> m_suppressed_double_click_releases;
	};
} // namespace AVPE
