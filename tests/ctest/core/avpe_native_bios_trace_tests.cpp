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

TEST(NativeBiosTraceTest, MissionBoundaryTimesOutWithoutGroundedReturn)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_TRUE(snapshot.empty());
	EXPECT_NE(AVPE::NativeBiosTrace::SnapshotJson().find("\"enabled\":false"),
		std::string::npos);
}
