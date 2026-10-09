#include "AVPE/NativeMinimap.h"

#include <gtest/gtest.h>

#include <bit>
#include <map>

namespace
{
	namespace NativeMinimap = AVPE::NativeMinimap;

	// M1 at 640x448: a 130-pixel map at (455, 309) over a 229-unit world.
	constexpr NativeMinimap::View M1{.left = 455.0f, .top = 309.0f, .width = 130.0f, .height = 130.0f, .world_per_pixel = 1.7615385f, .negated_origin_x = -0.0f, .negated_origin_y = -0.0f};

	TEST(NativeMinimapTest, MapsAMapPixelToTheWorld)
	{
		// The live camera target (180, 26) is drawn at about (557, 324).
		const auto point = NativeMinimap::WorldAt(M1, 557.0f, 324.0f);
		ASSERT_TRUE(point.has_value());
		EXPECT_NEAR(point->x, 179.7f, 0.1f);
		EXPECT_NEAR(point->y, 26.4f, 0.1f);
	}

	TEST(NativeMinimapTest, OffsetsByTheWorldOrigin)
	{
		NativeMinimap::View view = M1;
		view.negated_origin_x = 100.0f;
		view.negated_origin_y = -50.0f;
		const auto point = NativeMinimap::WorldAt(view, 455.0f, 309.0f);
		ASSERT_TRUE(point.has_value());
		EXPECT_FLOAT_EQ(point->x, -100.0f);
		EXPECT_FLOAT_EQ(point->y, 50.0f);
	}

	TEST(NativeMinimapTest, PointsOffTheMapHaveNoWorldPoint)
	{
		EXPECT_FALSE(NativeMinimap::WorldAt(M1, 454.0f, 320.0f).has_value());
		EXPECT_FALSE(NativeMinimap::WorldAt(M1, 500.0f, 440.0f).has_value());
	}

	TEST(NativeMinimapTest, NoViewWithoutAMinimapMenu)
	{
		std::map<u32, u32> words{{NativeMinimap::MinimapInstance, 0x017059C0}, {NativeMinimap::MinimapMenuInstance, 0}};
		const auto read = [&words](const u32 address, u32* value) {
			const auto found = words.find(address);
			if (found == words.end())
			{
				return false;
			}
			*value = found->second;
			return true;
		};
		NativeMinimap::View view;
		EXPECT_FALSE(NativeMinimap::ReadView(read, &view));
		words[NativeMinimap::MinimapMenuInstance] = 0x0151DE50;
		const auto set = [&words](const u32 offset, const float value) {
			words[0x017059C0 + offset] = std::bit_cast<u32>(value);
		};
		set(NativeMinimap::MapLeftOffset, 455.0f);
		set(NativeMinimap::MapTopOffset, 309.0f);
		set(NativeMinimap::MapWidthOffset, 130.0f);
		set(NativeMinimap::MapHeightOffset, 130.0f);
		set(NativeMinimap::WorldPerPixelOffset, 1.76f);
		set(NativeMinimap::NegatedOriginXOffset, 0.0f);
		set(NativeMinimap::NegatedOriginYOffset, 0.0f);
		ASSERT_TRUE(NativeMinimap::ReadView(read, &view));
		EXPECT_FLOAT_EQ(view.left, 455.0f);
	}
} // namespace
