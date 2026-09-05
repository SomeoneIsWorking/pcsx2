#include "AVPE/NativePadReadiness.h"

#include <gtest/gtest.h>

#include <map>

namespace
{
	class NativePadReadinessTest : public testing::Test
	{
	protected:
		static constexpr u32 device = 0x012EA290;
		static constexpr u32 backend = 0x012EA380;
		std::map<u32, u32> words = {
			{device, 0x00334870}, {device + 0x34, backend}, {backend, 0x003348C0},
			{backend + 0x404, 0}, {backend + 0x408, 0}, {backend + 0x40C, 0xAAAAAA51},
			{backend + 0x414, 6}, {backend + 0x41C, 0xFFFF7300}, {backend + 0x43C, 0xFFFF7300}};

		bool Ready()
		{
			return AVPE::NativePadReadiness::IsReady(device, [this](const u32 address, u32* value) {
				const auto found = words.find(address);
				if (found == words.end())
					return false;
				*value = found->second;
				return true;
			});
		}
	};

	TEST_F(NativePadReadinessTest, AcceptsLiveDigitalAnalogAndPressureReports)
	{
		for (const u32 report : {0xFFFF4100u, 0xFFBF7300u, 0xFFFF7900u})
		{
			words[backend + 0x41C] = report;
			words[backend + 0x43C] = report;
			EXPECT_TRUE(Ready());
		}
		words[backend + 0x414] = 2;
		EXPECT_TRUE(Ready());
	}

	TEST_F(NativePadReadinessTest, RejectsObservedTitleInitializationAndEachNegotiationPhase)
	{
		for (const u32 flags : {0x03u, 0x09u, 0x01u, 0x11u, 0x50u, 0x53u, 0x55u, 0x59u})
		{
			words[backend + 0x40C] = flags;
			EXPECT_FALSE(Ready()) << flags;
		}
		words[backend + 0x40C] = 0x51;
		for (const u32 state : {0u, 1u, 3u, 4u, 5u, 7u})
		{
			words[backend + 0x414] = state;
			EXPECT_FALSE(Ready()) << state;
		}
	}

	TEST_F(NativePadReadinessTest, RequiresBothReportsAfterModeSwitch)
	{
		for (const u32 offset : {0x41Cu, 0x43Cu})
		{
			for (const u32 invalid : {0u, 0xFFFF0000u, 0xFFFFFFFFu, 0xFFFF7301u})
			{
				words[backend + offset] = invalid;
				EXPECT_FALSE(Ready()) << offset << ": " << invalid;
			}
			words[backend + offset] = 0xFFFF7300;
		}
		EXPECT_TRUE(Ready());
	}

	TEST_F(NativePadReadinessTest, RejectsWrongOwnersPortSlotAndEveryUnreadableField)
	{
		const auto valid = words;
		for (const auto& [address, value] : valid)
		{
			words.erase(address);
			EXPECT_FALSE(Ready()) << address;
			words[address] = value;
		}
		for (const u32 address : {device, device + 0x34, backend, backend + 0x404, backend + 0x408})
		{
			words[address] = 1;
			EXPECT_FALSE(Ready()) << address;
			words = valid;
		}
	}
} // namespace
