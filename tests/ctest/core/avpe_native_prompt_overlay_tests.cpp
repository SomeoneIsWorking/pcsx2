#include "pcsx2/AVPE/NativePromptOverlay.h"

#include <gtest/gtest.h>

namespace
{
	using AVPE::DisplayRect;
	using AVPE::NativePromptOverlay;
	using AVPE::PromptFrame;
	using AVPE::PromptRect;
	using AVPE::NativeMenuInput::Action;

	PromptFrame PauseFrame()
	{
		PromptFrame frame;
		frame.framebuffer_width = 640.0f;
		frame.framebuffer_height = 448.0f;
		return frame;
	}

	TEST(NativePromptOverlayTest, MapsFramebufferRectIntoPresentedRect)
	{
		// The Back glyph's drawn quad, presented letterboxed at 2x into a 1280x960 rect.
		const PromptRect back{Action::Cancel, 158.0f, 383.0f, 182.0f, 407.0f};
		const DisplayRect display{100.0f, 20.0f, 1380.0f, 916.0f};

		const DisplayRect cover = NativePromptOverlay::MapToDisplay(back, PauseFrame(), display);

		const float margin = NativePromptOverlay::CoverMargin;
		EXPECT_FLOAT_EQ(cover.left, 100.0f + (158.0f - margin) * 2.0f);
		EXPECT_FLOAT_EQ(cover.right, 100.0f + (182.0f + margin) * 2.0f);
		EXPECT_FLOAT_EQ(cover.top, 20.0f + (383.0f - margin) * 2.0f);
		EXPECT_FLOAT_EQ(cover.bottom, 20.0f + (407.0f + margin) * 2.0f);
	}

	TEST(NativePromptOverlayTest, StretchesRowsByTheFramebufferHeightNotTheDisplayHeight)
	{
		// /snap and a 4:3 present both stretch the 448-row framebuffer to 480 rows.
		const PromptRect select{Action::Activate, 98.0f, 383.0f, 122.0f, 407.0f};
		const DisplayRect display{0.0f, 0.0f, 640.0f, 480.0f};

		const DisplayRect cover = NativePromptOverlay::MapToDisplay(select, PauseFrame(), display);

		EXPECT_FLOAT_EQ(cover.top, (383.0f - NativePromptOverlay::CoverMargin) * 480.0f / 448.0f);
	}

	TEST(PresentedDisplayTest, UnflipsLowerLeftOriginPresentRect)
	{
		const DisplayRect presented{0.0f, 100.0f, 640.0f, 580.0f};

		const DisplayRect top = AVPE::PresentedDisplay::TopOrigin(presented, true, 720.0f);

		EXPECT_FLOAT_EQ(top.top, 140.0f);
		EXPECT_FLOAT_EQ(top.bottom, 620.0f);
		EXPECT_FLOAT_EQ(top.left, 0.0f);
	}

	TEST(PresentedDisplayTest, KeepsUpperLeftOriginPresentRect)
	{
		const DisplayRect presented{0.0f, 100.0f, 640.0f, 580.0f};

		const DisplayRect top = AVPE::PresentedDisplay::TopOrigin(presented, false, 720.0f);

		EXPECT_FLOAT_EQ(top.top, 100.0f);
		EXPECT_FLOAT_EQ(top.bottom, 580.0f);
	}

	TEST(PresentedDisplayTest, NormalizesAPointerInsidePillarboxedImage)
	{
		// A 4:3 image centred in a 1920x1080 window spans x 240..1680.
		const DisplayRect display{240.0f, 0.0f, 1680.0f, 1080.0f};

		const AVPE::NormalizedPoint centre = AVPE::PresentedDisplay::Normalize(display, 960.0f, 540.0f);
		const AVPE::NormalizedPoint left_edge = AVPE::PresentedDisplay::Normalize(display, 240.0f, 0.0f);

		EXPECT_FLOAT_EQ(centre.x, 0.5f);
		EXPECT_FLOAT_EQ(centre.y, 0.5f);
		EXPECT_FLOAT_EQ(left_edge.x, 0.0f);
	}

	TEST(PresentedDisplayTest, ClampsAPointerInTheBarsToTheImageEdge)
	{
		const DisplayRect display{240.0f, 0.0f, 1680.0f, 1080.0f};

		const AVPE::NormalizedPoint left_bar = AVPE::PresentedDisplay::Normalize(display, 100.0f, 540.0f);
		const AVPE::NormalizedPoint right_bar = AVPE::PresentedDisplay::Normalize(display, 1900.0f, 540.0f);

		EXPECT_FLOAT_EQ(left_bar.x, 0.0f);
		EXPECT_FLOAT_EQ(right_bar.x, 1.0f);
	}
} // namespace
