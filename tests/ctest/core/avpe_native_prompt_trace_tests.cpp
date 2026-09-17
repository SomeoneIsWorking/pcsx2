#include "AVPE/NativePromptTrace.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace
{
	class NativePromptTraceTest : public testing::Test
	{
	protected:
		static constexpr u32 Base = 0x00100000;
		static constexpr u32 Font = Base + 0x100;
		static constexpr u32 Render = Base + 0x400;
		static constexpr u32 Resource = Base + 0x600;
		inline static NativePromptTraceTest* current = nullptr;

		std::array<u8, 0x1000> memory{};
		AVPE::NativePromptTrace trace;

		void SetUp() override
		{
			current = this;
			WriteWord(Render + 0x20, Resource);
			WriteText("Select");
			memory[Font - Base + 0x24 + 'S'] = 7;
		}

		void TearDown() override
		{
			current = nullptr;
		}

		static bool Read(const u32 address, void* const destination, const u32 size)
		{
			if (current == nullptr || address < Base || size > current->memory.size() ||
				address - Base > current->memory.size() - size)
			{
				return false;
			}
			std::memcpy(destination, current->memory.data() + address - Base, size);
			return true;
		}

		void WriteWord(const u32 address, const u32 value)
		{
			for (u32 index = 0; index < 4; ++index)
			{
				memory[address - Base + index] = static_cast<u8>(value >> (8 * index));
			}
		}

		void WriteText(const std::string_view value)
		{
			std::fill_n(memory.begin() + Resource - Base + 0x0C,
				AVPE::NativePromptTrace::TextBytes, 0);
			std::copy(value.begin(), value.end(), memory.begin() + Resource - Base + 0x0C);
		}

		void Observe()
		{
			trace.Observe(Font, Render, Read);
		}
	};
} // namespace

TEST_F(NativePromptTraceTest, CapturesLiveReaderInputsAndReportsInvalidAndTruncatedReads)
{
	EXPECT_TRUE(AVPE::NativePromptTrace::ShouldInstrumentEePc(0x001390E0));
	EXPECT_FALSE(AVPE::NativePromptTrace::ShouldInstrumentEePc(0x001390E4));
	Observe();
	EXPECT_EQ(trace.Capture().observed_calls, 0u);

	trace.Start();
	Observe();
	Observe();
	auto snapshot = trace.Capture();
	ASSERT_EQ(snapshot.fonts.size(), 1u);
	ASSERT_EQ(snapshot.texts.size(), 1u);
	EXPECT_EQ(snapshot.observed_calls, 2u);
	EXPECT_EQ(snapshot.fonts[0].glyph_map['S'], 7);
	EXPECT_EQ(snapshot.fonts[0].calls, 2u);
	EXPECT_EQ(snapshot.texts[0].calls, 2u);
	EXPECT_EQ(snapshot.texts[0].length, 6u);
	EXPECT_TRUE(std::equal(snapshot.texts[0].text.begin(), snapshot.texts[0].text.begin() + 6, "Select"));

	memory[Font - Base + 0x24 + 'S'] = 8;
	WriteText("Back");
	Observe();
	snapshot = trace.Capture();
	ASSERT_EQ(snapshot.fonts.size(), 2u);
	ASSERT_EQ(snapshot.texts.size(), 2u);
	EXPECT_EQ(snapshot.texts[1].font_index, 1u);
	EXPECT_EQ(snapshot.texts[1].length, 4u);

	WriteWord(Render + 0x20, 0);
	Observe();
	snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_calls, 4u);
	EXPECT_EQ(snapshot.invalid_reads, 1u);

	WriteWord(Render + 0x20, Resource);
	std::fill_n(memory.begin() + Resource - Base + 0x0C,
		AVPE::NativePromptTrace::TextBytes, 'x');
	Observe();
	snapshot = trace.Capture();
	EXPECT_EQ(snapshot.truncated_text_calls, 1u);
	ASSERT_EQ(snapshot.texts.size(), 3u);
	EXPECT_TRUE(snapshot.texts[2].truncated);
	EXPECT_EQ(snapshot.texts[2].length, AVPE::NativePromptTrace::TextBytes);

	trace.Stop();
	Observe();
	EXPECT_EQ(trace.Capture().observed_calls, 5u);
	trace.Reset();
	snapshot = trace.Capture();
	EXPECT_FALSE(snapshot.armed);
	EXPECT_EQ(snapshot.observed_calls, 0u);
	EXPECT_TRUE(snapshot.fonts.empty());
	EXPECT_TRUE(snapshot.texts.empty());
}

TEST_F(NativePromptTraceTest, CapsDistinctTextAndFontSamplesWithoutSilence)
{
	trace.Start();
	for (size_t index = 0; index <= AVPE::NativePromptTrace::MaxTexts; ++index)
	{
		memory[Resource - Base + 0x0C] = static_cast<u8>((index & 0xff) + 1);
		memory[Resource - Base + 0x0D] = static_cast<u8>((index >> 8) + 1);
		Observe();
	}
	auto snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_calls, AVPE::NativePromptTrace::MaxTexts + 1);
	EXPECT_EQ(snapshot.texts.size(), AVPE::NativePromptTrace::MaxTexts);
	EXPECT_EQ(snapshot.dropped_texts, 1u);
	EXPECT_EQ(snapshot.dropped_fonts, 0u);

	trace.Start();
	for (size_t index = 0; index <= AVPE::NativePromptTrace::MaxFonts; ++index)
	{
		memory[Font - Base + 0x24 + 'S'] = static_cast<u8>(index);
		Observe();
	}
	snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_calls, AVPE::NativePromptTrace::MaxFonts + 1);
	EXPECT_EQ(snapshot.fonts.size(), AVPE::NativePromptTrace::MaxFonts);
	EXPECT_EQ(snapshot.dropped_fonts, 1u);
}
