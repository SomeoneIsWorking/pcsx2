// AVPE product-host keyboard routing. Fork-local; not for upstream PCSX2.
#pragma once

#include <cstdint>
#include <unordered_set>

class QKeyEvent;

namespace AVPE
{
	class HostInputRouter final
	{
	public:
		enum class PointerButton : std::uint8_t
		{
			Primary,
			Secondary,
		};

		bool HandleKeyPress(const QKeyEvent& event);
		bool HandleKeyRelease(const QKeyEvent& event);
		bool HandlePointerMove(float normalized_x, float normalized_y);
		bool HandlePointerButton(PointerButton button, bool pressed);
		bool HandlePointerDoubleClick(PointerButton button);

	private:
		std::unordered_set<int> m_consumed_keys;
		std::unordered_set<PointerButton> m_menu_pointer_buttons;
		std::unordered_set<PointerButton> m_gameplay_pointer_buttons;
		std::unordered_set<PointerButton> m_suppressed_double_click_releases;
	};
} // namespace AVPE
