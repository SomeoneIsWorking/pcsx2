// Where the guest image sits in the host window. Fork-local.

#pragma once

#include <mutex>
#include <optional>

namespace AVPE
{
	// Window-pixel rectangle, top-left origin.
	struct DisplayRect
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	struct NormalizedPoint
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	// The GS thread stores the rect each present; the overlay and the host pointer read it.
	class PresentedDisplay final
	{
	public:
		// The present rect is flipped on lower-left-origin devices.
		static DisplayRect TopOrigin(const DisplayRect& presented, bool lower_left_origin, float window_height);
		// A window position as a 0..1 position in the guest image, clamped to its edges.
		static NormalizedPoint Normalize(const DisplayRect& display, float window_x, float window_y);

		void Store(const DisplayRect& display);
		std::optional<DisplayRect> Load() const;

		static PresentedDisplay& Process();

	private:
		mutable std::mutex m_mutex;
		std::optional<DisplayRect> m_display;
	};
} // namespace AVPE
