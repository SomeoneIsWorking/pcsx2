#include "AVPE/NativeStatPanel.h"

#include <gtest/gtest.h>

#include <map>

namespace
{
	using AVPE::NativeStatPanel;

	class NativeStatPanelTest : public testing::Test
	{
	protected:
		static constexpr u32 sp = 0x01F00000;
		static constexpr u32 result = sp + NativeStatPanel::HoverResultStackOffset;
		static constexpr u32 pointer = 0x01005000;
		static constexpr u32 selection = 0x01006000;
		static constexpr u32 data = 0x01006100;
		static constexpr u32 mark = 0x01006200;
		static constexpr u32 unit = 0x01007000;
		static constexpr u32 unit_handle = 0x00470000;

		std::map<u32, u32> words;
		NativeStatPanel::Access access;

		void SetUp() override
		{
			words[result] = 0;
			words[NativeStatPanel::PointerInstance] = pointer;
			words[pointer + NativeStatPanel::SelectionOffset] = selection;
			words[selection] = data;
			words[selection + 4] = 2;
			words[data] = mark;
			words[mark + NativeStatPanel::MarkObjectOffset] = unit;
			words[unit + NativeStatPanel::ObjectHandleOffset] = unit_handle;
			access.read = [this](const u32 address, u32* value) {
				const auto found = words.find(address);
				if (found == words.end())
					return false;
				*value = found->second;
				return true;
			};
			access.write = [this](const u32 address, const u32 value) {
				words[address] = value;
				return true;
			};
		}
	};

	TEST_F(NativeStatPanelTest, WithNothingHoveredThePanelShowsTheFirstSelectedUnit)
	{
		EXPECT_TRUE(NativeStatPanel::ObserveHoverResult(sp, access));
		EXPECT_EQ(words[result], unit_handle);
	}

	TEST_F(NativeStatPanelTest, AHoveredObjectKeepsThePanel)
	{
		words[result] = 0x00120000;
		EXPECT_FALSE(NativeStatPanel::ObserveHoverResult(sp, access));
		EXPECT_EQ(words[result], 0x00120000u);
	}

	TEST_F(NativeStatPanelTest, AnEmptySelectionLeavesThePanelEmpty)
	{
		words[selection + 4] = 0;
		EXPECT_FALSE(NativeStatPanel::ObserveHoverResult(sp, access));
		EXPECT_EQ(words[result], 0u);
		words.erase(NativeStatPanel::PointerInstance);
		EXPECT_FALSE(NativeStatPanel::ObserveHoverResult(sp, access));
	}
} // namespace
