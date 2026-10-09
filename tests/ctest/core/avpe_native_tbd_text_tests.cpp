#include "AVPE/NativeTbdText.h"

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>

namespace
{
	using AVPE::NativeTbdText;
	using Status = NativeTbdText::Status;
	constexpr const NativeTbdText::ConfirmPrompt& title = NativeTbdText::ConfirmPrompts[0];

	class NativeTbdTextTest : public testing::Test
	{
	protected:
		static constexpr u32 table = 0x00367358;
		static constexpr u32 buckets = 0x01000000;
		static constexpr u32 entries = 0x01100000;
		static constexpr u32 text = 0x015474B0;
		static constexpr u32 symbol = 0x9BD83674;
		std::map<u32, u8> memory;
		NativeTbdText::Access access;

		void SetUp() override
		{
			Word(NativeTbdText::SymbolTablePointer, table);
			Word(table, 0xFF);
			Word(table + 4, buckets);
			const u32 bucket = buckets + (symbol & 0xFF) * 12;
			Word(bucket, entries);
			Word(bucket + 4, 1);
			Word(bucket + 8, 0x4B);
			Word(entries, 0x01200000);
			Word(entries + 4, 0x12345678);
			Word(entries + 12, text);
			Word(entries + 16, symbol);
			Text(text, std::string("Press START button") + '\0');
			access.read = [this](const u32 address, void* destination, const u32 size) {
				auto* bytes = static_cast<u8*>(destination);
				for (u32 index = 0; index < size; ++index)
				{
					const auto found = memory.find(address + index);
					if (found == memory.end())
						return false;
					bytes[index] = found->second;
				}
				return true;
			};
			access.write = [this](const u32 address, const void* source, const u32 size) {
				const auto* bytes = static_cast<const u8*>(source);
				for (u32 index = 0; index < size; ++index)
					memory[address + index] = bytes[index];
				return true;
			};
		}

		void Word(const u32 address, const u32 value)
		{
			for (u32 index = 0; index < 4; ++index)
				memory[address + index] = static_cast<u8>(value >> (index * 8));
		}

		void Text(const u32 address, const std::string& value)
		{
			for (u32 index = 0; index < value.size(); ++index)
				memory[address + index] = static_cast<u8>(value[index]);
		}

		void Publish(const u32 crc, const u32 address, const std::string_view value)
		{
			const u32 bucket = buckets + (crc & 0xFF) * 12;
			const u32 entry = entries + 0x1000;
			Word(bucket, entry);
			Word(bucket + 4, 0);
			Word(bucket + 8, 1);
			Word(entry, address);
			Word(entry + 4, crc);
			Text(address, std::string(value) + '\0');
		}

		std::string Read(const u32 address, const u32 size)
		{
			std::string value(size, '\0');
			for (u32 index = 0; index < size; ++index)
				value[index] = static_cast<char>(memory[address + index]);
			return value;
		}
	};

	TEST_F(NativeTbdTextTest, NamesTheConfirmKeyInTheTitlePrompt)
	{
		EXPECT_EQ(NativeTbdText::RewriteConfirmPrompt(title, "Enter", access), Status::Rewritten);
		EXPECT_EQ(Read(text, 19), std::string("Press Enter") + std::string(8, '\0'));
		EXPECT_EQ(NativeTbdText::RewriteConfirmPrompt(title, "Enter", access), Status::AlreadyNative);
	}

	TEST_F(NativeTbdTextTest, NamesTheConfirmKeyInBothLoadErrorTexts)
	{
		for (const NativeTbdText::ConfirmPrompt& prompt :
			{NativeTbdText::ConfirmPrompts[1], NativeTbdText::ConfirmPrompts[2]})
		{
			constexpr u32 error_text = 0x009F1E4C;
			Publish(prompt.symbol, error_text, prompt.original);
			const u32 size = static_cast<u32>(prompt.original.size());
			EXPECT_EQ(NativeTbdText::RewriteConfirmPrompt(prompt, "Enter", access), Status::Rewritten);
			const std::string expected = std::string(prompt.original.substr(0, size - 30)) + "Press Enter to continue";
			EXPECT_EQ(Read(error_text, size), expected + std::string(size - expected.size(), '\0'));
		}
		EXPECT_EQ(Read(0x009F1E4C, 22), "Error Loading Level!\nA");
	}

	TEST_F(NativeTbdTextTest, LeavesOtherBytesAndMissingSymbolsAlone)
	{
		Text(text, std::string("DEMO MODE") + '\0');
		EXPECT_EQ(NativeTbdText::RewriteConfirmPrompt(title, "Enter", access), Status::Foreign);
		EXPECT_EQ(Read(text, 10), std::string("DEMO MODE") + '\0');
		Word(entries + 16, 0x11111111);
		EXPECT_EQ(NativeTbdText::RewriteConfirmPrompt(title, "Enter", access), Status::Absent);
		Word(NativeTbdText::SymbolTablePointer, 0);
		EXPECT_EQ(NativeTbdText::RewriteConfirmPrompt(title, "Enter", access), Status::Unreadable);
	}

	TEST_F(NativeTbdTextTest, NeverGrowsTheStringOrWritesWithoutALabel)
	{
		EXPECT_EQ(NativeTbdText::RewriteConfirmPrompt(title, "", access), Status::Foreign);
		EXPECT_EQ(NativeTbdText::RewriteConfirmPrompt(title, "Very Long Key", access), Status::Foreign);
		EXPECT_EQ(Read(text, 18), "Press START button");
	}
} // namespace
