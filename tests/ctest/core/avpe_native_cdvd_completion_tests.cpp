#include "AVPE/NativeCdvdCompletion.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
	class NativeCdvdCompletionTest : public testing::Test
	{
	protected:
		void SetUp() override
		{
			AVPE::NativeCdvdCompletion::Reset();
		}
	};
} // namespace

TEST_F(NativeCdvdCompletionTest, SameStackConsumesExactlyOnce)
{
	ASSERT_TRUE(AVPE::NativeCdvdCompletion::Record(0x1000, -5));
	const std::optional<std::int32_t> completion = AVPE::NativeCdvdCompletion::Consume(0x1000);
	ASSERT_TRUE(completion);
	EXPECT_EQ(*completion, -5);
	EXPECT_FALSE(AVPE::NativeCdvdCompletion::Consume(0x1000));

	const AVPE::NativeCdvdCompletion::Snapshot snapshot = AVPE::NativeCdvdCompletion::GetSnapshot();
	EXPECT_EQ(snapshot.recorded, 1u);
	EXPECT_EQ(snapshot.consumed, 1u);
	EXPECT_EQ(snapshot.consume_misses, 1u);
	EXPECT_EQ(snapshot.rejected_records, 0u);
	EXPECT_EQ(snapshot.active_tokens, 0u);
}

TEST_F(NativeCdvdCompletionTest, DistinctStacksRemainIndependentWhenInterleaved)
{
	ASSERT_TRUE(AVPE::NativeCdvdCompletion::Record(0x1000, -1));
	ASSERT_TRUE(AVPE::NativeCdvdCompletion::Record(0x2000, -2));

	EXPECT_EQ(AVPE::NativeCdvdCompletion::Consume(0x2000), -2);
	EXPECT_EQ(AVPE::NativeCdvdCompletion::Consume(0x1000), -1);
	EXPECT_EQ(AVPE::NativeCdvdCompletion::GetSnapshot().active_tokens, 0u);
}

TEST_F(NativeCdvdCompletionTest, RecordingSameStackReplacesPendingResultInPlace)
{
	ASSERT_TRUE(AVPE::NativeCdvdCompletion::Record(0x1000, -1));
	ASSERT_TRUE(AVPE::NativeCdvdCompletion::Record(0x1000, -9));

	const AVPE::NativeCdvdCompletion::Snapshot pending = AVPE::NativeCdvdCompletion::GetSnapshot();
	EXPECT_EQ(pending.recorded, 2u);
	EXPECT_EQ(pending.active_tokens, 1u);
	EXPECT_EQ(AVPE::NativeCdvdCompletion::Consume(0x1000), -9);
}

TEST_F(NativeCdvdCompletionTest, FullStoreRejectsNewStackWithoutEvictingExistingTokens)
{
	for (std::uint32_t index = 0; index < AVPE::NativeCdvdCompletion::Capacity; ++index)
		ASSERT_TRUE(AVPE::NativeCdvdCompletion::Record(0x1000 + index, -static_cast<std::int32_t>(index)));
	EXPECT_FALSE(AVPE::NativeCdvdCompletion::Record(0x3000, -99));

	const AVPE::NativeCdvdCompletion::Snapshot full = AVPE::NativeCdvdCompletion::GetSnapshot();
	EXPECT_EQ(full.recorded, AVPE::NativeCdvdCompletion::Capacity);
	EXPECT_EQ(full.rejected_records, 1u);
	EXPECT_EQ(full.active_tokens, AVPE::NativeCdvdCompletion::Capacity);
	for (std::uint32_t index = 0; index < AVPE::NativeCdvdCompletion::Capacity; ++index)
		EXPECT_EQ(AVPE::NativeCdvdCompletion::Consume(0x1000 + index), -static_cast<std::int32_t>(index));
}

TEST_F(NativeCdvdCompletionTest, WrongStackMissIsTheOtherAnswerAndPreservesToken)
{
	ASSERT_TRUE(AVPE::NativeCdvdCompletion::Record(0x1000, -5));
	EXPECT_FALSE(AVPE::NativeCdvdCompletion::Consume(0x2000));
	EXPECT_EQ(AVPE::NativeCdvdCompletion::GetSnapshot().active_tokens, 1u);
	EXPECT_EQ(AVPE::NativeCdvdCompletion::Consume(0x1000), -5);
}

TEST_F(NativeCdvdCompletionTest, ResetClearsTokensAndCounters)
{
	ASSERT_TRUE(AVPE::NativeCdvdCompletion::Record(0x1000, -5));
	EXPECT_FALSE(AVPE::NativeCdvdCompletion::Consume(0x2000));
	AVPE::NativeCdvdCompletion::Reset();

	const AVPE::NativeCdvdCompletion::Snapshot snapshot = AVPE::NativeCdvdCompletion::GetSnapshot();
	EXPECT_EQ(snapshot.recorded, 0u);
	EXPECT_EQ(snapshot.consumed, 0u);
	EXPECT_EQ(snapshot.consume_misses, 0u);
	EXPECT_EQ(snapshot.rejected_records, 0u);
	EXPECT_EQ(snapshot.active_tokens, 0u);
	EXPECT_FALSE(AVPE::NativeCdvdCompletion::Consume(0x1000));
}
