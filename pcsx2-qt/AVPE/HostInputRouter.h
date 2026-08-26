// AVPE product-host keyboard routing. Fork-local; not for upstream PCSX2.
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

	private:
		std::unordered_set<int> m_consumed_keys;
	};
} // namespace AVPE
