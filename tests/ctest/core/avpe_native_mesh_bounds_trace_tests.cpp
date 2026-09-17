#include "AVPE/NativeMeshBoundsTrace.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

namespace
{
	class NativeMeshBoundsTraceTest : public testing::Test
	{
	protected:
		static constexpr u32 Base = 0x00100000;
		static constexpr u32 Workspace = Base + 0x100;
		static constexpr u32 Sp = Base + 0x800;
		static constexpr u32 Render = Base + 0x400;
		static constexpr u32 BoundsObject = Base + 0x500;
		inline static NativeMeshBoundsTraceTest* current = nullptr;

		std::array<u8, 0x2000> memory{};
		AVPE::NativeMeshBoundsTrace trace;

		void SetUp() override
		{
			current = this;
			WriteWord(Workspace + 0x04, Render);
			WriteWord(Workspace + 0x08, BoundsObject);
			WriteRect(102, 431, 118, 413);
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

		void WriteRect(const s32 xmax, const s32 ymax, const s32 xmin, const s32 ymin)
		{
			WriteWord(Sp + AVPE::NativeMeshBoundsTrace::RectStackOffset + 0x00, static_cast<u32>(xmax));
			WriteWord(Sp + AVPE::NativeMeshBoundsTrace::RectStackOffset + 0x04, static_cast<u32>(ymax));
			WriteWord(Sp + AVPE::NativeMeshBoundsTrace::RectStackOffset + 0x08, static_cast<u32>(xmin));
			WriteWord(Sp + AVPE::NativeMeshBoundsTrace::RectStackOffset + 0x0C, static_cast<u32>(ymin));
		}

		void Observe()
		{
			trace.Observe(Workspace, Sp, Read);
		}
	};
} // namespace

TEST_F(NativeMeshBoundsTraceTest, CapturesLiveRectAndIdentityAndReportsInvalidReads)
{
	EXPECT_TRUE(AVPE::NativeMeshBoundsTrace::ShouldInstrumentEePc(0x00136320));
	EXPECT_FALSE(AVPE::NativeMeshBoundsTrace::ShouldInstrumentEePc(0x00136318));
	Observe();
	EXPECT_EQ(trace.Capture().observed_calls, 0u);

	trace.Start();
	Observe();
	Observe();
	auto snapshot = trace.Capture();
	ASSERT_EQ(snapshot.samples.size(), 1u);
	EXPECT_EQ(snapshot.observed_calls, 2u);
	EXPECT_EQ(snapshot.samples[0].workspace, Workspace);
	EXPECT_EQ(snapshot.samples[0].render, Render);
	EXPECT_EQ(snapshot.samples[0].bounds_object, BoundsObject);
	EXPECT_EQ(snapshot.samples[0].xmax, 102);
	EXPECT_EQ(snapshot.samples[0].ymax, 431);
	EXPECT_EQ(snapshot.samples[0].xmin, 118);
	EXPECT_EQ(snapshot.samples[0].ymin, 413);
	EXPECT_EQ(snapshot.samples[0].calls, 2u);

	WriteRect(162, 428, 177, 413);
	Observe();
	snapshot = trace.Capture();
	ASSERT_EQ(snapshot.samples.size(), 2u);
	EXPECT_EQ(snapshot.samples[1].xmax, 162);
	EXPECT_EQ(snapshot.samples[1].calls, 1u);

	WriteWord(Workspace + 0x04, 0);
	Observe();
	snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_calls, 4u);
	EXPECT_EQ(snapshot.invalid_reads, 1u);

	trace.Stop();
	WriteWord(Workspace + 0x04, Render);
	Observe();
	EXPECT_EQ(trace.Capture().observed_calls, 4u);

	trace.Reset();
	snapshot = trace.Capture();
	EXPECT_FALSE(snapshot.armed);
	EXPECT_EQ(snapshot.observed_calls, 0u);
	EXPECT_TRUE(snapshot.samples.empty());
}

TEST_F(NativeMeshBoundsTraceTest, CapsDistinctSamplesWithoutSilence)
{
	trace.Start();
	for (size_t index = 0; index <= AVPE::NativeMeshBoundsTrace::MaxSamples; ++index)
	{
		WriteRect(static_cast<s32>(index), 1, 2, 3);
		Observe();
	}
	const auto snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_calls, AVPE::NativeMeshBoundsTrace::MaxSamples + 1);
	EXPECT_EQ(snapshot.samples.size(), AVPE::NativeMeshBoundsTrace::MaxSamples);
	EXPECT_EQ(snapshot.dropped_samples, 1u);
}

TEST_F(NativeMeshBoundsTraceTest, RejectsImplausibleWorkspaceOrStackAddress)
{
	trace.Start();
	trace.Observe(0, Sp, Read);
	trace.Observe(Workspace, 0, Read);
	const auto snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_calls, 2u);
	EXPECT_EQ(snapshot.invalid_reads, 2u);
	EXPECT_TRUE(snapshot.samples.empty());
}
