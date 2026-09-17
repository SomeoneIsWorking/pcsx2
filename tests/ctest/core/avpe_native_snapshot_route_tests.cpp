#include "AVPE/NativeSnapshotRoute.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

TEST(NativeSnapshotRouteTest, PreservesBottomUpBgrAndPaddedRowBmpContract)
{
	const std::array<u32, 4> top_then_bottom{0x112233, 0x445566, 0x778899, 0xAABBCC};
	const auto bmp = AVPE::NativeSnapshotRoute::EncodeBmp(2, 2, top_then_bottom);
	ASSERT_TRUE(bmp);
	ASSERT_EQ(bmp->size(), 70u);
	EXPECT_EQ((*bmp)[0], 'B');
	EXPECT_EQ((*bmp)[1], 'M');
	EXPECT_EQ((*bmp)[2], 70);
	EXPECT_EQ((*bmp)[10], 54);
	EXPECT_EQ((*bmp)[14], 40);
	EXPECT_EQ((*bmp)[18], 2);
	EXPECT_EQ((*bmp)[22], 2);
	EXPECT_EQ((*bmp)[26], 1);
	EXPECT_EQ((*bmp)[28], 24);
	EXPECT_EQ((*bmp)[34], 16);
	const std::array<u8, 16> rows{0x99, 0x88, 0x77, 0xCC, 0xBB, 0xAA, 0, 0,
		0x33, 0x22, 0x11, 0x66, 0x55, 0x44, 0, 0};
	EXPECT_TRUE(std::equal(rows.begin(), rows.end(), bmp->begin() + 54));
	EXPECT_FALSE(AVPE::NativeSnapshotRoute::EncodeBmp(0, 2, top_then_bottom));
	EXPECT_FALSE(AVPE::NativeSnapshotRoute::EncodeBmp(2, 2, std::span(top_then_bottom).first(3)));
}
