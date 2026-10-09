// AVP:E minimap geometry. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <functional>
#include <optional>

namespace AVPE::NativeMinimap
{
	inline constexpr u32 MinimapInstance = 0x00367F40;
	inline constexpr u32 MinimapMenuInstance = 0x00367F44;
	// GMiniMap::SetUpCorners: the drawn map's top-left and size in guest pixels, world
	// units per pixel, and the negated world origin.
	inline constexpr u32 MapLeftOffset = 0xE68;
	inline constexpr u32 MapTopOffset = 0xE6C;
	inline constexpr u32 MapWidthOffset = 0xE50;
	inline constexpr u32 MapHeightOffset = 0xE54;
	inline constexpr u32 WorldPerPixelOffset = 0xE4C;
	inline constexpr u32 NegatedOriginXOffset = 0xE60;
	inline constexpr u32 NegatedOriginYOffset = 0xE64;

	struct View
	{
		float left = 0.0f;
		float top = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		float world_per_pixel = 0.0f;
		float negated_origin_x = 0.0f;
		float negated_origin_y = 0.0f;
	};

	struct WorldPoint
	{
		float x;
		float y;
	};

	// False when no minimap is shown or its state is unreadable.
	bool ReadView(const std::function<bool(u32, u32*)>& read, View* view);
	// The world point under a guest-pixel screen point, if it is on the map.
	std::optional<WorldPoint> WorldAt(const View& view, float screen_x, float screen_y);
} // namespace AVPE::NativeMinimap
