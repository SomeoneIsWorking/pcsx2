#include "AVPE/NativeDragSelect.h"

#include <gtest/gtest.h>

#include <bit>
#include <map>
#include <utility>

namespace
{
	using AVPE::NativeDragSelect;

	class NativeDragSelectTest : public testing::Test
	{
	protected:
		static constexpr u32 pointer = 0x0152AB30;
		std::map<u32, u32> words;
		NativeDragSelect drag;
		NativeDragSelect::Access access;

		void SetUp() override
		{
			words[NativeDragSelect::PointerInstance] = pointer;
			words[pointer + NativeDragSelect::InputTypeOffset] = NativeDragSelect::AbsoluteInputType;
			access.read = [this](const u32 address, u32* value) {
				const auto found = words.find(address);
				if (found == words.end())
				{
					return false;
				}
				*value = found->second;
				return true;
			};
			access.write = [this](const u32 address, const u32 value) {
				words[address] = value;
				return true;
			};
		}

		void Set(const u32 offset, const float x, const float y)
		{
			words[pointer + offset] = std::bit_cast<u32>(x);
			words[pointer + offset + 4] = std::bit_cast<u32>(y);
		}

		std::pair<float, float> Get(const u32 offset)
		{
			return {std::bit_cast<float>(words[pointer + offset]), std::bit_cast<float>(words[pointer + offset + 4])};
		}

		// GAvPPointer after SelectChanging(true) at x, y.
		void Press(const float x, const float y)
		{
			words[pointer + NativeDragSelect::GrowingOffset] = 1;
			Set(NativeDragSelect::BoxMinOffset, x, y);
			Set(NativeDragSelect::BoxMaxOffset, x, y);
			Set(NativeDragSelect::CursorOffset, x, y);
		}

		// One UpdateGrowBox step (about half a pixel at 60 Hz), then the cursor moved by the absolute path.
		void Frame(const float cursor_x, const float cursor_y)
		{
			const auto [min_x, min_y] = Get(NativeDragSelect::BoxMinOffset);
			Set(NativeDragSelect::BoxMinOffset, min_x - 0.5f, min_y - 0.5f);
			Set(NativeDragSelect::BoxMaxOffset, cursor_x, cursor_y);
			Set(NativeDragSelect::CursorOffset, cursor_x, cursor_y);
			drag.ObserveGrowBox(access);
		}
	};

	TEST_F(NativeDragSelectTest, BoxSpansThePressPointAndTheCursor)
	{
		Press(255.0f, 178.0f);
		Frame(255.0f, 178.0f);
		Frame(400.0f, 300.0f);
		Frame(472.0f, 337.0f);
		EXPECT_EQ(Get(NativeDragSelect::BoxMinOffset), std::make_pair(254.5f, 177.5f));
		EXPECT_EQ(Get(NativeDragSelect::BoxMaxOffset), std::make_pair(472.0f, 337.0f));
	}

	TEST_F(NativeDragSelectTest, DraggingUpAndLeftKeepsTheCornersOrdered)
	{
		Press(255.0f, 178.0f);
		Frame(255.0f, 178.0f);
		Frame(100.0f, 50.0f);
		EXPECT_EQ(Get(NativeDragSelect::BoxMinOffset), std::make_pair(100.0f, 50.0f));
		EXPECT_EQ(Get(NativeDragSelect::BoxMaxOffset), std::make_pair(254.5f, 177.5f));
	}

	TEST_F(NativeDragSelectTest, ANewPressAnchorsAtItsOwnPoint)
	{
		Press(255.0f, 178.0f);
		Frame(300.0f, 200.0f);
		words[pointer + NativeDragSelect::GrowingOffset] = 0;
		drag.ObserveGrowBox(access);
		Press(50.0f, 60.0f);
		Frame(80.0f, 90.0f);
		EXPECT_EQ(Get(NativeDragSelect::BoxMinOffset), std::make_pair(49.5f, 59.5f));
		EXPECT_EQ(Get(NativeDragSelect::BoxMaxOffset), std::make_pair(80.0f, 90.0f));
	}

	TEST_F(NativeDragSelectTest, PadSelectionStillGrows)
	{
		words[pointer + NativeDragSelect::InputTypeOffset] = 2;
		Press(255.0f, 178.0f);
		Frame(255.0f, 178.0f);
		EXPECT_EQ(Get(NativeDragSelect::BoxMinOffset), std::make_pair(254.5f, 177.5f));
	}
} // namespace
