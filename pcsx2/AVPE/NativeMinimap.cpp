// AVP:E minimap geometry. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMinimap.h"

#include <bit>
#include <cmath>

namespace AVPE::NativeMinimap
{
	bool ReadView(const std::function<bool(u32, u32*)>& read, View* view)
	{
		u32 minimap = 0;
		u32 menu = 0;
		if (!read(MinimapInstance, &minimap) || minimap == 0 || !read(MinimapMenuInstance, &menu) || menu == 0)
		{
			return false;
		}
		const auto field = [&read, minimap](const u32 offset, float* value) {
			u32 bits = 0;
			if (!read(minimap + offset, &bits))
			{
				return false;
			}
			*value = std::bit_cast<float>(bits);
			return std::isfinite(*value);
		};
		return field(MapLeftOffset, &view->left) && field(MapTopOffset, &view->top) &&
		       field(MapWidthOffset, &view->width) && field(MapHeightOffset, &view->height) &&
		       field(WorldPerPixelOffset, &view->world_per_pixel) &&
		       field(NegatedOriginXOffset, &view->negated_origin_x) &&
		       field(NegatedOriginYOffset, &view->negated_origin_y) && view->width > 0.0f && view->height > 0.0f;
	}

	std::optional<WorldPoint> WorldAt(const View& view, const float screen_x, const float screen_y)
	{
		const float x = screen_x - view.left;
		const float y = screen_y - view.top;
		if (x < 0.0f || y < 0.0f || x > view.width || y > view.height)
		{
			return std::nullopt;
		}
		// GMiniMap::GetCamPointerPos.
		return WorldPoint{x * view.world_per_pixel - view.negated_origin_x, y * view.world_per_pixel - view.negated_origin_y};
	}
} // namespace AVPE::NativeMinimap
