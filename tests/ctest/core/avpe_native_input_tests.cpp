#include "AVPE/NativeInput.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace
{
	namespace NativeInput = AVPE::NativeInput;
	using NativeInput::SelectionMode;

	constexpr u32 pointer = 0x0152AB30;
	constexpr u32 menu = 0x015B03E0;

	std::vector<std::pair<u32, u64>> Calls(const SelectionMode mode)
	{
		std::vector<std::pair<u32, u64>> calls;
		for (const AVPE::EECallShuttle::Request& request : NativeInput::PrimaryReleaseCalls(mode, pointer, menu))
		{
			calls.emplace_back(request.function, request.arguments[2]);
		}
		return calls;
	}

	TEST(NativeInputTest, PlainReleaseIsTheGuestMouseHandler)
	{
		const std::vector<std::pair<u32, u64>> expected{{NativeInput::ReleaseMousePrimaryFunction, 0}};
		EXPECT_EQ(Calls(SelectionMode::Replace), expected);
	}

	TEST(NativeInputTest, ShiftReleaseSelectsAdditivelyThenRefreshesTheMenu)
	{
		const std::vector<std::pair<u32, u64>> expected{
			{NativeInput::SelectChangingFunction, 1}, {NativeInput::InGameMenuRefreshFunction, 0}};
		EXPECT_EQ(Calls(SelectionMode::Toggle), expected);
		EXPECT_EQ(NativeInput::PrimaryReleaseCalls(SelectionMode::Toggle, pointer, menu)[1].arguments[0], menu);
	}

	TEST(NativeInputTest, CtrlReleaseSelectsThenWidensToTheType)
	{
		const std::vector<std::pair<u32, u64>> expected{{NativeInput::SelectChangingFunction, 0},
			{NativeInput::DoubleClickSelectChangingFunction, 0}, {NativeInput::InGameMenuRefreshFunction, 0}};
		EXPECT_EQ(Calls(SelectionMode::SameType), expected);
	}
} // namespace
