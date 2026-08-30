#include "AVPE/NativeBiosTrace.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

TEST(NativeBiosTraceTest, DisabledTraceDoesNotRetainEvents)
{
	AVPE::NativeBiosTrace::SetEnabled(false);
	AVPE::NativeBiosTrace::RecordImport("ioman", 6, "read", 1, 2, 3, 4, 0, true, false);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"enabled\":false"), std::string::npos);
	EXPECT_NE(snapshot.find("\"events\":[]"), std::string::npos);
}

TEST(NativeBiosTraceTest, RecordsOrderedEventsAndReturnStatus)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordModule("ioman", 2, 0, "register");
	AVPE::NativeBiosTrace::RecordInterrupt(0, "INT_VBLANK", 0x1234);
	AVPE::NativeBiosTrace::RecordRpc(0x80000100);
	AVPE::NativeBiosTrace::RecordEeSyscall(34, "StartThread", 5, 6, 7, 8, -2);
	AVPE::NativeBiosTrace::RecordException("ee", 8, 0x1000, true);
	AVPE::NativeBiosTrace::RecordTimer("iop", 2, true, 0x10001, 0xffff, 1234, false);
	AVPE::NativeBiosTrace::RecordTimer("ee", 1, false, 10, 10, 42, true);
	AVPE::NativeBiosTrace::RecordImport("ioman", 6, "read", 1, 2, 3, 4, -1, false, false);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"sequence\":1"), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"module\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"interrupt\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"rpc\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"ee_syscall\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"name\":\"StartThread\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"arguments\":[5,6,7,8]"), std::string::npos);
	EXPECT_NE(snapshot.find("\"result\":-2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"exception\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"domain\":\"ee\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"branch_delay\":true"), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"timer\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"counter\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"overflow\":true"), std::string::npos);
	EXPECT_NE(snapshot.find("\"delivered\":false"), std::string::npos);
	EXPECT_NE(snapshot.find("\"domain\":\"ee\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"delivered\":true"), std::string::npos);
	EXPECT_NE(snapshot.find("\"result\":-1"), std::string::npos);
	EXPECT_NE(snapshot.find("\"hle\":false"), std::string::npos);
}

TEST(NativeBiosTraceTest, EnforcesBoundedCapacity)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	for (u32 i = 0; i < AVPE::NativeBiosTrace::MaximumEvents + 3; ++i)
		AVPE::NativeBiosTrace::RecordRpc(i);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"overflow\":3"), std::string::npos);
	EXPECT_NE(snapshot.find("\"rpc_id\":4095"), std::string::npos);
	EXPECT_EQ(snapshot.find("\"rpc_id\":4096"), std::string::npos);
}

TEST(NativeBiosTraceTest, CoalescesRepeatedImportIdentity)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordImport("ioman", 6, "read", 1, 2, 3, 4, 0, true, false);
	AVPE::NativeBiosTrace::RecordImport("ioman", 6, "read", 5, 6, 7, 8, -1, true, false);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"calls\":2"), std::string::npos);
	EXPECT_EQ(snapshot.find("\"sequence\":2"), std::string::npos);
}

TEST(NativeBiosTraceTest, CoalescesRepeatedTimerShape)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordTimer("ee", 1, true, 10, 20, 30, false);
	AVPE::NativeBiosTrace::RecordTimer("ee", 1, true, 40, 50, 60, false);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"calls\":2"), std::string::npos);
	EXPECT_EQ(snapshot.find("\"sequence\":2"), std::string::npos);
}

TEST(NativeBiosTraceTest, CoalescesRepeatedSyscallAndExceptionIdentity)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordEeSyscall(122, "sceSifGetReg", 1, 2, 3, 4, 0);
	AVPE::NativeBiosTrace::RecordEeSyscall(122, "sceSifGetReg", 5, 6, 7, 8, 0);
	AVPE::NativeBiosTrace::RecordException("ee", 32, 0x1234, false);
	AVPE::NativeBiosTrace::RecordException("ee", 32, 0x1234, false);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"calls\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"sequence\":2"), std::string::npos);
	EXPECT_EQ(snapshot.find("\"sequence\":3"), std::string::npos);
}

TEST(NativeBiosTraceTest, CaptureDisablesFurtherEvents)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordImport("ioman", 6, "read", 1, 2, 3, 4, 0, true, false);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotAndDisableJson();
	AVPE::NativeBiosTrace::RecordRpc(99);
	const std::string after = AVPE::NativeBiosTrace::SnapshotJson();

	EXPECT_NE(snapshot.find("\"enabled\":true"), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"import\""), std::string::npos);
	EXPECT_NE(after.find("\"enabled\":false"), std::string::npos);
	EXPECT_EQ(after.find("\"rpc_id\":99"), std::string::npos);
}

TEST(NativeBiosTraceTest, GuestBoundaryCaptureTimesOutWithoutBoundary)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordRpc(7);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureAtGuestBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_TRUE(snapshot.empty());
	EXPECT_NE(AVPE::NativeBiosTrace::SnapshotJson().find("\"rpc_id\":7"), std::string::npos);
}

TEST(NativeBiosTraceTest, GuestBoundaryWithoutRequestPreservesTrace)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordRpc(7);
	AVPE::NativeBiosTrace::OnGuestFrameBoundary();

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();

	EXPECT_NE(snapshot.find("\"enabled\":true"), std::string::npos);
	EXPECT_NE(snapshot.find("\"rpc_id\":7"), std::string::npos);
}

TEST(NativeBiosTraceTest, MissionBoundaryCapturesGroundedEntryAndReturn)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::RecordRpc(7);
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016FA4C);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_NE(snapshot.find("\"mission_boundary\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"entry_pc\":1505552"), std::string::npos);
	EXPECT_NE(snapshot.find("\"return_pc\":1505868"), std::string::npos);
	EXPECT_NE(snapshot.find("\"sequence_errors\":0"), std::string::npos);
	EXPECT_NE(snapshot.find("\"rpc_id\":7"), std::string::npos);
}

TEST(NativeBiosTraceTest, MissionBoundaryInstrumentationCoversGroundedPcsBeforeArming)
{
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x0016F910));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x0016FA4C));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00173770));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00173CB0));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00173D90));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00173D98));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00173E34));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00173FFC));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00174164));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00174178));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00174180));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00174188));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00174190));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00174198));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x001741E0));
	EXPECT_FALSE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x0016FA48));
}

TEST(NativeBiosTraceTest, MissionBoundaryReportsBoundedTbdChunkProgress)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173CB0, 0x00280000, 0, 0, false);
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x00173CB0);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173D90, 0, 0x002047B0, 0x00280000, true);
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x00173D90);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173D98, 0, 0, 0, false);
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x00173D98);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173D90, 0, 0x002047B0, 0x00180000, true);
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x00173D90);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173D98, 0, 0, 0, false);
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x00173D98);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173E34, 0, 0, 0, false);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00173FFC, 1);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00173FFC, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174164, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174178, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174180, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174188, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174190, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174198, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x001741E0, 0);
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x00173E34);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173CB0, 32, 0, 0, false);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173D90, 0, 0x002047B0, 32, true);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173D98, 0, 0, 0, false);
	AVPE::NativeBiosTrace::ObserveMissionLoadProgress(0x00173E34, 0, 0, 0, false);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00173FFC, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174164, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174178, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174180, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174188, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174190, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174198, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x001741E0, 0);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_NE(snapshot.find("\"load_progress\":{\"chunks_started\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"chunks_completed\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"callbacks\":3"), std::string::npos);
	EXPECT_NE(snapshot.find("\"callback_pc\":2115504"), std::string::npos);
	EXPECT_NE(snapshot.find("\"invalid_remaining_reads\":0"), std::string::npos);
	EXPECT_NE(snapshot.find("\"payload_bytes\":2621472"), std::string::npos);
	EXPECT_NE(snapshot.find("\"payload_chunks\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"multi_slice_chunks\":1"), std::string::npos);
	EXPECT_NE(snapshot.find("\"last_payload_size\":32"), std::string::npos);
	EXPECT_NE(snapshot.find("\"max_payload_size\":2621440"), std::string::npos);
	EXPECT_NE(snapshot.find("\"timing_sequence_errors\":0"), std::string::npos);
	EXPECT_NE(snapshot.find("\"first_chunk_entry\":{\"pc\":1522864"), std::string::npos);
	EXPECT_NE(snapshot.find("\"last_chunk_return\":{\"pc\":1523252"), std::string::npos);
	EXPECT_NE(snapshot.find("\"chunk_timing\":{\"samples\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"callback_timing\":{\"samples\":3"), std::string::npos);
	EXPECT_NE(snapshot.find("\"payload_timing\":{\"samples\":3"), std::string::npos);
	EXPECT_NE(snapshot.find("\"inter_chunk_timing\":{\"samples\":1"), std::string::npos);
	EXPECT_NE(snapshot.find("\"post_read_progress\":{\"sequence_errors\":0,\"next_expected\":\"eof_detected\",\"active_depth\":0"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"eof_detected\":{\"count\":2,\"last\":{\"pc\":1523708"), std::string::npos);
	EXPECT_NE(snapshot.find("\"watch_released\":{\"count\":2,\"last\":{\"pc\":1524068"), std::string::npos);
	EXPECT_NE(snapshot.find("\"fixup_offsets_complete\":{\"count\":2,\"last\":{\"pc\":1524088"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"init_types_complete\":{\"count\":2,\"last\":{\"pc\":1524120"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"load_core_return\":{\"count\":2,\"last\":{\"pc\":1524192"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"complete\":false"), std::string::npos);
	EXPECT_NE(snapshot.find("\"return\":null"), std::string::npos);
}

TEST(NativeBiosTraceTest, MissionBoundaryReportsTbdLoadErrorWithoutWaitingForReturn)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);
	AVPE::NativeBiosTrace::ObserveMissionLoadError(0x00173770, 0x002D6E38, 0x001738B4);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_NE(snapshot.find("\"complete\":false"), std::string::npos);
	EXPECT_NE(snapshot.find("\"load_error\":{\"argument\":2977336"), std::string::npos);
	EXPECT_NE(snapshot.find("\"return_pc\":1521844"), std::string::npos);
	EXPECT_NE(snapshot.find("\"pc\":1521520"), std::string::npos);
	EXPECT_NE(snapshot.find("\"enabled\":true"), std::string::npos);
}

TEST(NativeBiosTraceTest, MissionPostReadProgressRejectsSkippedStage)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174164, 0);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_NE(snapshot.find("\"post_read_progress\":{\"sequence_errors\":1"), std::string::npos);
	EXPECT_NE(snapshot.find("\"eof_detected\":{\"count\":0,\"last\":null"), std::string::npos);
	EXPECT_NE(snapshot.find("\"watch_released\":{\"count\":1,\"last\":{\"pc\":1524068"), std::string::npos);
}

TEST(NativeBiosTraceTest, MissionPostReadProgressTracksNestedLoadCore)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);
	for (const u32 pc : {0x00173FFC, 0x00174164, 0x00174178, 0x00174180, 0x00174188, 0x00174190})
		AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(pc, 0);
	for (const u32 pc : {0x00173FFC, 0x00174164, 0x00174178, 0x00174180,
			 0x00174188, 0x00174190, 0x00174198, 0x001741E0})
		AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(pc, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x00174198, 0);
	AVPE::NativeBiosTrace::ObserveMissionPostReadProgress(0x001741E0, 0);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_NE(snapshot.find("\"post_read_progress\":{\"sequence_errors\":0,\"next_expected\":\"eof_detected\",\"active_depth\":0"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"eof_detected\":{\"count\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"init_types_complete\":{\"count\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"load_core_return\":{\"count\":2"), std::string::npos);
}

TEST(NativeBiosTraceTest, MissionBoundaryTimesOutWithoutGroundedReturn)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_NE(snapshot.find("\"mission_boundary\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"complete\":false"), std::string::npos);
	EXPECT_NE(snapshot.find("\"entry\":{"), std::string::npos);
	EXPECT_NE(snapshot.find("\"return\":null"), std::string::npos);
	EXPECT_NE(snapshot.find("\"enabled\":false"), std::string::npos);
}
