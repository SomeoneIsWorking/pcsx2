#include "AVPE/NativeMeshBoundsTrace.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <span>

namespace
{
	class NativeMeshBoundsTraceTest : public testing::Test
	{
	protected:
		static constexpr u32 Base = 0x00100000;
		static constexpr u32 Workspace = Base + 0x100;
		static constexpr u32 WorkspaceVtable = Base + 0x300;
		static constexpr u32 Sp = Base + 0x800;
		static constexpr u32 Render = Base + 0x400;
		static constexpr u32 BoundsObject = Base + 0x500;
		static constexpr u32 Resource = Base + 0x600;
		static constexpr u32 OtherResource = Base + 0x700;
		static constexpr u32 GetMatrix = 0x001362C0;
		static constexpr u32 RenderImpl = 0x001884E0;
		inline static NativeMeshBoundsTraceTest* current = nullptr;

		std::array<u8, 0x2000> memory{};
		AVPE::NativeMeshBoundsTrace trace;

		void SetUp() override
		{
			current = this;
			WriteWord(Workspace + 0x04, Render);
			WriteWord(Workspace + 0x08, BoundsObject);
			WriteWord(Workspace, WorkspaceVtable);
			WriteWord(WorkspaceVtable + 0x14, GetMatrix);
			WriteWord(Render + 0x20, Workspace);
			// GetScreenBoundingBox__13CRendBaseMesh writes minX, minY, maxX, maxY.
			WriteRect(98, 391, 122, 415);
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

		void WriteRect(const s32 xmin, const s32 ymin, const s32 xmax, const s32 ymax)
		{
			WriteWord(Sp + AVPE::NativeMeshBoundsTrace::RectStackOffset + 0x00, static_cast<u32>(xmin));
			WriteWord(Sp + AVPE::NativeMeshBoundsTrace::RectStackOffset + 0x04, static_cast<u32>(ymin));
			WriteWord(Sp + AVPE::NativeMeshBoundsTrace::RectStackOffset + 0x08, static_cast<u32>(xmax));
			WriteWord(Sp + AVPE::NativeMeshBoundsTrace::RectStackOffset + 0x0C, static_cast<u32>(ymax));
		}

		void ArmWith(const u32 resource)
		{
			const std::array<u32, 1> admitted{resource};
			trace.Start(std::span<const u32>(admitted));
		}

		void ObserveRect()
		{
			trace.ObserveGetMatrixRect(Workspace, Sp, Read);
		}

		void ObserveDispatch(const u32 resource)
		{
			trace.ObserveRenderDispatch(resource, Render, RenderImpl, Read);
		}
	};
} // namespace

TEST_F(NativeMeshBoundsTraceTest, InstrumentsTheRenderDisplayDispatchAndTheRectReturn)
{
	EXPECT_TRUE(AVPE::NativeMeshBoundsTrace::ShouldInstrumentEePc(AVPE::NativeMeshBoundsTrace::RenderDisplayDispatchPc));
	EXPECT_TRUE(AVPE::NativeMeshBoundsTrace::ShouldInstrumentEePc(AVPE::NativeMeshBoundsTrace::GetMatrixRectReturnPc));
	EXPECT_FALSE(AVPE::NativeMeshBoundsTrace::ShouldInstrumentEePc(0x001370A8));
}

TEST_F(NativeMeshBoundsTraceTest, CapturesRectInMinMaxOrderAndReportsInvalidReads)
{
	ArmWith(BoundsObject);
	ObserveRect();
	ObserveRect();
	auto snapshot = trace.Capture();
	ASSERT_EQ(snapshot.samples.size(), 1u);
	EXPECT_EQ(snapshot.observed_rects, 2u);
	EXPECT_EQ(snapshot.matched_rects, 2u);
	EXPECT_EQ(snapshot.samples[0].workspace, Workspace);
	EXPECT_EQ(snapshot.samples[0].render, Render);
	EXPECT_EQ(snapshot.samples[0].bounds_object, BoundsObject);
	EXPECT_EQ(snapshot.samples[0].xmin, 98);
	EXPECT_EQ(snapshot.samples[0].ymin, 391);
	EXPECT_EQ(snapshot.samples[0].xmax, 122);
	EXPECT_EQ(snapshot.samples[0].ymax, 415);
	EXPECT_EQ(snapshot.samples[0].calls, 2u);

	WriteRect(158, 391, 182, 415);
	ObserveRect();
	snapshot = trace.Capture();
	ASSERT_EQ(snapshot.samples.size(), 2u);
	EXPECT_EQ(snapshot.samples[1].xmin, 158);
	EXPECT_EQ(snapshot.samples[1].xmax, 182);
	EXPECT_EQ(snapshot.samples[1].calls, 1u);

	WriteWord(Workspace + 0x04, 0);
	ObserveRect();
	snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_rects, 4u);
	EXPECT_EQ(snapshot.invalid_reads, 1u);

	trace.Stop();
	WriteWord(Workspace + 0x04, Render);
	ObserveRect();
	EXPECT_EQ(trace.Capture().observed_rects, 4u);

	trace.Reset();
	snapshot = trace.Capture();
	EXPECT_FALSE(snapshot.armed);
	EXPECT_EQ(snapshot.observed_rects, 0u);
	EXPECT_TRUE(snapshot.samples.empty());
}

TEST_F(NativeMeshBoundsTraceTest, IgnoresDisarmedObservations)
{
	ObserveRect();
	ObserveDispatch(Resource);
	const auto snapshot = trace.Capture();
	EXPECT_FALSE(snapshot.armed);
	EXPECT_EQ(snapshot.observed_rects, 0u);
	EXPECT_EQ(snapshot.observed_dispatches, 0u);
}

TEST_F(NativeMeshBoundsTraceTest, CountsUnadmittedDrawsRatherThanStoringThem)
{
	ArmWith(Resource);
	ObserveDispatch(OtherResource);
	ObserveDispatch(OtherResource);
	auto snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_dispatches, 2u);
	EXPECT_EQ(snapshot.matched_dispatches, 0u);
	EXPECT_TRUE(snapshot.dispatches.empty());

	ObserveDispatch(Resource);
	snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_dispatches, 3u);
	EXPECT_EQ(snapshot.matched_dispatches, 1u);
	ASSERT_EQ(snapshot.dispatches.size(), 1u);
	EXPECT_EQ(snapshot.dispatches[0].resource, Resource);
	EXPECT_EQ(snapshot.dispatches[0].render_node, Render);
	EXPECT_EQ(snapshot.dispatches[0].render, RenderImpl);
	EXPECT_EQ(snapshot.dispatches[0].workspace, Workspace);
	EXPECT_EQ(snapshot.dispatches[0].get_matrix, GetMatrix);

	ObserveDispatch(Resource);
	EXPECT_EQ(trace.Capture().dispatches[0].calls, 2u);
}

TEST_F(NativeMeshBoundsTraceTest, RejectsImplausibleDispatchAndRectIdentity)
{
	// A null address is not a real resource, so arming with it admits nothing.
	ArmWith(0);
	ObserveDispatch(0);
	ObserveDispatch(Resource);
	auto snapshot = trace.Capture();
	EXPECT_TRUE(snapshot.admitted.empty());
	EXPECT_EQ(snapshot.observed_dispatches, 2u);
	EXPECT_EQ(snapshot.matched_dispatches, 0u);
	EXPECT_TRUE(snapshot.dispatches.empty());

	ArmWith(Resource);
	WriteWord(Render + 0x20, 0);
	ObserveDispatch(Resource);
	snapshot = trace.Capture();
	EXPECT_EQ(snapshot.matched_dispatches, 1u);
	EXPECT_EQ(snapshot.invalid_dispatches, 1u);
	EXPECT_TRUE(snapshot.dispatches.empty());

	ArmWith(BoundsObject);
	trace.ObserveGetMatrixRect(0, Sp, Read);
	trace.ObserveGetMatrixRect(Workspace, 0, Read);
	const auto rejected = trace.Capture();
	EXPECT_EQ(rejected.observed_rects, 2u);
	EXPECT_EQ(rejected.invalid_reads, 2u);
	EXPECT_TRUE(rejected.samples.empty());
}

TEST_F(NativeMeshBoundsTraceTest, ArmsWithNoResourcesAndRecordsNothing)
{
	const std::span<const u32> empty;
	trace.Start(empty);
	ObserveDispatch(Resource);
	ObserveRect();
	const auto snapshot = trace.Capture();
	EXPECT_TRUE(snapshot.admitted.empty());
	EXPECT_EQ(snapshot.observed_dispatches, 1u);
	EXPECT_EQ(snapshot.matched_dispatches, 0u);
	EXPECT_EQ(snapshot.observed_rects, 1u);
	EXPECT_EQ(snapshot.matched_rects, 0u);
}

TEST_F(NativeMeshBoundsTraceTest, CapsDistinctRectsWithoutSilence)
{
	ArmWith(BoundsObject);
	for (size_t index = 0; index <= AVPE::NativeMeshBoundsTrace::MaxSamples; ++index)
	{
		WriteRect(static_cast<s32>(index), 1, 2, 3);
		ObserveRect();
	}
	const auto snapshot = trace.Capture();
	EXPECT_EQ(snapshot.observed_rects, AVPE::NativeMeshBoundsTrace::MaxSamples + 1);
	EXPECT_EQ(snapshot.samples.size(), AVPE::NativeMeshBoundsTrace::MaxSamples);
	EXPECT_EQ(snapshot.dropped_samples, 1u);
}