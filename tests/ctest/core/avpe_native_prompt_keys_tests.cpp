#include "pcsx2/AVPE/NativePromptKeys.h"

#include <gtest/gtest.h>

namespace
{
	using AVPE::NativePromptKeys;
	using AVPE::PromptButton;
	using AVPE::PromptKey;

	TEST(NativePromptKeysTest, ConfirmAndBackFollowTheItemsHotkey)
	{
		EXPECT_EQ(NativePromptKeys::KindFor(NativePromptKeys::FrontEndSelect, PromptButton::Cross), PromptKey::Kind::Confirm);
		EXPECT_EQ(NativePromptKeys::KindFor(NativePromptKeys::FrontEndBack, PromptButton::Triangle), PromptKey::Kind::Back);
		EXPECT_EQ(NativePromptKeys::KindFor(NativePromptKeys::MenuTriangleRelease, PromptButton::Triangle),
			PromptKey::Kind::Back);
		// Gather is drawn with the Triangle glyph but is a command.
		EXPECT_EQ(NativePromptKeys::KindFor(0xB8697E8E, PromptButton::Triangle), PromptKey::Kind::Command);
	}

	TEST(NativePromptKeysTest, AnItemWithoutHotkeyFollowsItsGlyph)
	{
		EXPECT_EQ(NativePromptKeys::KindFor(0, PromptButton::Cross), PromptKey::Kind::Confirm);
		EXPECT_EQ(NativePromptKeys::KindFor(0, PromptButton::Triangle), PromptKey::Kind::Back);
		EXPECT_EQ(NativePromptKeys::KindFor(0, PromptButton::Square), PromptKey::Kind::Command);
	}

	TEST(NativePromptKeysTest, CommandsTakeTheFirstFreeLetterOfTheirLabel)
	{
		NativePromptKeys keys;
		const PromptKey select = keys.Next(NativePromptKeys::FrontEndSelect, PromptButton::Cross, "Select");
		EXPECT_EQ(select.kind, PromptKey::Kind::Confirm);
		EXPECT_EQ(keys.Next(0xC134080A, PromptButton::Cross, "Patrol").letter, 'P');
		EXPECT_EQ(keys.Next(0x52D7E2FE, PromptButton::Circle, "Prev").letter, 'R');
		EXPECT_EQ(keys.Next(0xCFF24A30, PromptButton::Square, "Stats Screen").letter, 'S');
		// Confirm and back items take no letter, so Select left S free above.
		EXPECT_EQ(keys.Next(0xC13B2F25, PromptButton::R1, "pre").letter, 'E');
		EXPECT_EQ(keys.Next(0xEE97F8F3, PromptButton::Circle, "").letter, 'A');
	}
} // namespace
