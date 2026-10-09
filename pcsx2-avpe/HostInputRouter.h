// AVPE product-host input routing. Fork-local; not for upstream PCSX2.
#pragma once

#include <unordered_set>

class QKeyEvent;

namespace AVPE
{
	class HostInputRouter final
	{
	public:
		bool HandleKeyPress(const QKeyEvent& event);
		bool HandleKeyRelease(const QKeyEvent& event);
		void Tick();

	private:
		// A letter triggers the prompted item it names this frame.
		bool HandleCommandKey(const QKeyEvent& event);
		// Control groups, the event and base jumps; only when no navigation menu is active.
		bool HandleUnitKey(const QKeyEvent& event);
		bool ApplyCameraMove(float x, float y);

		std::unordered_set<int> m_consumed_keys;
		std::unordered_set<int> m_camera_keys;
	};
} // namespace AVPE
