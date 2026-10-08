// Where the guest image sits in the host window. Fork-local.

#include "AVPE/PresentedDisplay.h"

#include <algorithm>

namespace AVPE
{
	namespace
	{
		PresentedDisplay s_process_display;
	} // namespace

	DisplayRect PresentedDisplay::TopOrigin(
		const DisplayRect& presented, const bool lower_left_origin, const float window_height)
	{
		if (!lower_left_origin)
		{
			return presented;
		}
		const float height = presented.bottom - presented.top;
		const float top = window_height - presented.bottom;
		return {presented.left, top, presented.right, top + height};
	}

	NormalizedPoint PresentedDisplay::Normalize(const DisplayRect& display, const float window_x, const float window_y)
	{
		const float width = std::max(display.right - display.left, 1.0f);
		const float height = std::max(display.bottom - display.top, 1.0f);
		return {
			std::clamp((window_x - display.left) / width, 0.0f, 1.0f),
			std::clamp((window_y - display.top) / height, 0.0f, 1.0f),
		};
	}

	void PresentedDisplay::Store(const DisplayRect& display)
	{
		std::scoped_lock lock(m_mutex);
		m_display = display;
	}

	std::optional<DisplayRect> PresentedDisplay::Load() const
	{
		std::scoped_lock lock(m_mutex);
		return m_display;
	}

	PresentedDisplay& PresentedDisplay::Process()
	{
		return s_process_display;
	}
} // namespace AVPE
