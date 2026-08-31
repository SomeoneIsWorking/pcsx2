#include "AVPE/NativeBiosTrace.h"
#include "AVPE/NativeHostYield.h"
#include "AVPE/NativeIopExecutionHooks.h"
#include "AVPE/NativeIopReturnSites.h"
#include "AVPE/NativeMenuInput.h"
#include "R3000A.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

TEST(NativeBiosTraceTest, DisabledTraceDoesNotRetainEvents)
{
	AVPE::NativeBiosTrace::SetEnabled(false);
	AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"ioman", 6, "read", 1, 2, 3, 4, true, false, 0x1000, 0x2000);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"enabled\":false"), std::string::npos);
	EXPECT_NE(snapshot.find("\"events\":[]"), std::string::npos);
}

TEST(NativeBiosTraceTest, RecordsOrderedEventsAndOnlyGroundedResults)
{
	using Disposition = AVPE::NativeBiosTrace::EeSyscallDisposition;
	using Outcome = AVPE::NativeBiosTrace::EeSyscallOutcome;

	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordModule("ioman", 2, 0, "register");
	AVPE::NativeBiosTrace::RecordInterrupt(0, "INT_VBLANK", 0x1234);
	AVPE::NativeBiosTrace::RecordRpc(0x80000100);
	AVPE::NativeBiosTrace::RecordEeSyscall(
		34, "StartThread", 5, 6, 7, 8, -2, 0, Outcome::Bios, Disposition::ReturningResult);
	AVPE::NativeBiosTrace::RecordEeSyscall(
		100, "FlushCache", 9, 10, 11, 12, -7, 0, Outcome::DirectNoResult,
		Disposition::ReturningNoResult);
	AVPE::NativeBiosTrace::RecordEeSyscall(
		60, "GetMemorySize", 13, 14, 15, 16, 0x02000000, 0, Outcome::DirectResult,
		Disposition::ReturningResult);
	AVPE::NativeBiosTrace::RecordException("ee", 8, 0x1000, true);
	AVPE::NativeBiosTrace::RecordTimer("iop", 2, true, 0x10001, 0xffff, 1234, false);
	AVPE::NativeBiosTrace::RecordTimer("ee", 1, false, 10, 10, 42, true);
	AVPE::NativeBiosTrace::RecordHandledIopImport(
		"ioman", 6, "read", 1, 2, 3, 4, -1, true, false);
	AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"cdvdman", 8, "sceCdGetError", 5, 6, 7, 8, true, false, 0x1000, 0x2000);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"schema\":\"avpe-bios-trace-v5\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"sequence\":1"), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"module\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"interrupt\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"rpc\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"ee_syscall\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"name\":\"StartThread\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"first_arguments\":[5,6,7,8]"), std::string::npos);
	EXPECT_NE(snapshot.find("\"outcome\":\"bios\",\"result_valid\":false"), std::string::npos);
	EXPECT_NE(snapshot.find("\"result_expected\":true,\"return_expected\":true"),
		std::string::npos);
	EXPECT_EQ(snapshot.find("\"result\":-2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"outcome\":\"direct\",\"result_valid\":false"),
		std::string::npos);
	EXPECT_EQ(snapshot.find("\"result\":-7"), std::string::npos);
	EXPECT_NE(snapshot.find(
				  "\"outcome\":\"direct\",\"result_valid\":true,\"result\":33554432"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"exception\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"domain\":\"ee\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"branch_delay\":true"), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"timer\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"counter\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"overflow\":true"), std::string::npos);
	EXPECT_NE(snapshot.find("\"delivered\":false"), std::string::npos);
	EXPECT_NE(snapshot.find("\"domain\":\"ee\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"delivered\":true"), std::string::npos);
	EXPECT_NE(snapshot.find("\"outcome\":\"hle\",\"result_valid\":true,\"result\":-1"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"outcome\":\"oracle\",\"result_valid\":false"), std::string::npos);
	EXPECT_EQ(snapshot.find("\"result\":123"), std::string::npos);
	EXPECT_NE(snapshot.find("\"hle_available\":true"), std::string::npos);
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
	AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"ioman", 6, "read", 1, 2, 3, 4, true, false, 0x1000, 0x2000);
	AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"ioman", 6, "read", 5, 6, 7, 8, true, false, 0x1000, 0x2000);
	AVPE::NativeBiosTrace::RecordHandledIopImport(
		"ioman", 6, "read", 9, 10, 11, 12, -1, true, false);
	AVPE::NativeBiosTrace::RecordHandledIopImport(
		"ioman", 6, "read", 13, 14, 15, 16, 8, true, false);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"calls\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"sequence\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"sequence\":3"), std::string::npos);
	EXPECT_EQ(snapshot.find("\"sequence\":4"), std::string::npos);
	EXPECT_EQ(snapshot.find("\"result\":99"), std::string::npos);
}

TEST(NativeBiosTraceTest, PairsIopOracleReturnByStackAndResumePc)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"cdvdman", 8, "sceCdGetError", 1, 2, 3, 4, true, false, 0x001FF000, 0x00010200);
	AVPE::NativeBiosTrace::RecordIopOracleImportReturn(0x001FE000, 0x00010200, 99);
	AVPE::NativeBiosTrace::RecordIopOracleImportReturn(0x001FF000, 0x00010204, 99);
	const u32 saved_sp = psxRegs.GPR.n.sp;
	const u32 saved_v0 = psxRegs.GPR.n.v0;
	psxRegs.GPR.n.sp = 0x001FF000;
	psxRegs.GPR.n.v0 = static_cast<u32>(-5);
	AVPE::NativeIopExecutionHooks::ObserveIopExecution(0x00010200);
	psxRegs.GPR.n.sp = saved_sp;
	psxRegs.GPR.n.v0 = saved_v0;

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"kind\":\"iop_import_return\""), std::string::npos);
	EXPECT_NE(snapshot.find(
				  "\"function\":\"sceCdGetError\",\"result_valid\":true,\"result\":-5"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"first_stack_pointer\":2093056"), std::string::npos);
	EXPECT_NE(snapshot.find("\"first_resume_pc\":66048"), std::string::npos);
	EXPECT_NE(snapshot.find(
				  "\"iop_import_pairing\":{\"entries\":1,\"returns\":1,\"pending\":0,\"overflow\":0}"),
		std::string::npos);
}

TEST(NativeBiosTraceTest, RegistersEachIopOracleReturnSiteOnce)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	EXPECT_TRUE(AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"cdvdman", 8, "sceCdGetError", 0, 0, 0, 0, true, false, 0x001FF000, 0x007FF000));
	EXPECT_TRUE(AVPE::NativeIopReturnSites::Contains(0x007FF000));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldObserveIopImportReturn(0x007FF000));
	EXPECT_FALSE(AVPE::NativeIopReturnSites::Contains(0x007FF004));
	EXPECT_FALSE(AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"cdvdman", 8, "sceCdGetError", 0, 0, 0, 0, true, false, 0x001FE000, 0x007FF000));
	AVPE::NativeBiosTrace::RecordIopOracleImportReturn(0x001FE000, 0x007FF000, 0);
	AVPE::NativeBiosTrace::RecordIopOracleImportReturn(0x001FF000, 0x007FF000, 0);
	EXPECT_FALSE(AVPE::NativeBiosTrace::ShouldObserveIopImportReturn(0x007FF000));
}

TEST(NativeBiosTraceTest, RejectsUnobservableIopReturnWithoutLeakingPendingState)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	EXPECT_FALSE(AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"cdvdman", 8, "sceCdGetError", 0, 0, 0, 0, true, false, 0x001FF000, 0));

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find(
				  "\"iop_import_pairing\":{\"entries\":1,\"returns\":0,\"pending\":0,\"overflow\":1}"),
		std::string::npos);
}

TEST(NativeBiosTraceTest, NestedIopOracleReturnsUseMostRecentMatchingEntry)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"outer", 1, "first", 0, 0, 0, 0, false, true, 0x001FF000, 0x00010200);
	AVPE::NativeBiosTrace::RecordIopOracleImportEntry(
		"inner", 2, "second", 0, 0, 0, 0, false, true, 0x001FF000, 0x00010200);
	AVPE::NativeBiosTrace::RecordIopOracleImportReturn(0x001FF000, 0x00010200, 7);
	AVPE::NativeBiosTrace::RecordIopOracleImportReturn(0x001FF000, 0x00010200, 8);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"library\":\"inner\",\"ordinal\":2,\"function\":\"second\","
							"\"result_valid\":true,\"result\":7"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"library\":\"outer\",\"ordinal\":1,\"function\":\"first\","
							"\"result_valid\":true,\"result\":8"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"entries\":2,\"returns\":2,\"pending\":0"),
		std::string::npos);
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
	using Disposition = AVPE::NativeBiosTrace::EeSyscallDisposition;
	using Outcome = AVPE::NativeBiosTrace::EeSyscallOutcome;

	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordEeSyscall(122, "sceSifGetReg", 1, 2, 3, 4, 99, 0,
		Outcome::Bios, Disposition::ReturningResult);
	AVPE::NativeBiosTrace::RecordEeSyscall(122, "sceSifGetReg", 5, 6, 7, 8, 0, 0,
		Outcome::Bios, Disposition::ReturningResult);
	AVPE::NativeBiosTrace::RecordException("ee", 32, 0x1234, false);
	AVPE::NativeBiosTrace::RecordException("ee", 32, 0x1234, false);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"calls\":2"), std::string::npos);
	EXPECT_NE(snapshot.find("\"sequence\":2"), std::string::npos);
	EXPECT_EQ(snapshot.find("\"sequence\":3"), std::string::npos);
}

TEST(NativeBiosTraceTest, PairsBiosReturnsByStackAndResumePc)
{
	using Disposition = AVPE::NativeBiosTrace::EeSyscallDisposition;

	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallEntry(
		68, "WaitSema", 10, 0, 0, 0, 0x01FFF000, 0x00102004,
		Disposition::ReturningResult);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallEntry(
		66, "SignalSema", 10, 0, 0, 0, 0x01FFE000, 0x00103004,
		Disposition::ReturningResult);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallReturn(0x01FFD000, 0x00104004, 99, 0);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallReturn(0x01FFE000, 0x00103004, 0, 0);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"kind\":\"ee_syscall_return\""), std::string::npos);
	EXPECT_NE(snapshot.find(
				  "\"name\":\"SignalSema\",\"result_expected\":true,\"result_valid\":true,\"result\":0"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"first_stack_pointer\":33546240"), std::string::npos);
	EXPECT_NE(snapshot.find("\"first_resume_pc\":1060868"), std::string::npos);
	EXPECT_NE(snapshot.find(
				  "\"ee_syscall_pairing\":{\"entries\":2,\"returns\":1,\"pending\":1,"
				  "\"sequence_errors\":0,\"overflow\":0}"),
		std::string::npos);
}

TEST(NativeBiosTraceTest, RejectsMismatchedBiosReturnBoundary)
{
	using Disposition = AVPE::NativeBiosTrace::EeSyscallDisposition;

	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallEntry(
		68, "WaitSema", 10, 0, 0, 0, 0x01FFF000, 0x00102004,
		Disposition::ReturningResult);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallReturn(0x01FFF000, 0x00102008, 0, 0);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"returns\":0,\"pending\":1,\"sequence_errors\":1"),
		std::string::npos);
	EXPECT_EQ(snapshot.find("\"kind\":\"ee_syscall_return\""), std::string::npos);
}

TEST(NativeBiosTraceTest, NonreturningBiosControlTransferDoesNotEnterPairingState)
{
	using Disposition = AVPE::NativeBiosTrace::EeSyscallDisposition;

	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallEntry(
		5, "ResumeIntrDispatch", 0, 0, 0, 0, 0x01FFF000, 0x00102004,
		Disposition::NonReturning);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"name\":\"ResumeIntrDispatch\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"return_expected\":false"), std::string::npos);
	EXPECT_NE(snapshot.find("\"result_expected\":false"), std::string::npos);
	EXPECT_NE(snapshot.find(
				  "\"ee_syscall_pairing\":{\"entries\":0,\"returns\":0,\"pending\":0,"
				  "\"sequence_errors\":0,\"overflow\":0}"),
		std::string::npos);
}

TEST(NativeBiosTraceTest, ReturningVoidAndU64ResultsRemainDistinct)
{
	using Disposition = AVPE::NativeBiosTrace::EeSyscallDisposition;

	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallEntry(
		100, "FlushCache", 0, 0, 0, 0, 0x01FFF000, 0x00102004,
		Disposition::ReturningNoResult);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallReturn(0x01FFF000, 0x00102004, 2, 0);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallEntry(
		112, "GsGetIMR", 0, 0, 0, 0, 0x01FFE000, 0x00103004,
		Disposition::ReturningU64Result);
	AVPE::NativeBiosTrace::RecordEeBiosSyscallReturn(
		0x01FFE000, 0x00103004, -1, 0x100000001ULL);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find(
				  "\"name\":\"FlushCache\",\"result_expected\":false,\"result_valid\":false"),
		std::string::npos);
	EXPECT_NE(snapshot.find(
				  "\"name\":\"GsGetIMR\",\"result_expected\":true,\"result_valid\":true,\"result_u64\":4294967297"),
		std::string::npos);
	EXPECT_EQ(snapshot.find("\"result\":2"), std::string::npos);
	EXPECT_EQ(snapshot.find("\"result\":-1"), std::string::npos);
	EXPECT_NE(snapshot.find("\"entries\":2,\"returns\":2,\"pending\":0"),
		std::string::npos);
}

TEST(NativeBiosTraceTest, CaptureDisablesFurtherEvents)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::RecordHandledIopImport(
		"ioman", 6, "read", 1, 2, 3, 4, 0, true, false);

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
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x0017467C));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00174684));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00127EC0));
	EXPECT_TRUE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x00127EC8));
	EXPECT_FALSE(AVPE::NativeBiosTrace::ShouldInstrumentMissionBoundary(0x0016FA48));
}

TEST(NativeHostYieldTest, InstrumentsOnlyGroundedMissionGoalsInputCadence)
{
	EXPECT_TRUE(AVPE::NativeHostYield::ShouldInstrumentEePc(0x002052C8));
	EXPECT_FALSE(AVPE::NativeHostYield::ShouldInstrumentEePc(0x002052C4));
	EXPECT_FALSE(AVPE::NativeHostYield::ShouldInstrumentEePc(0x002052CC));
}

TEST(NativeMenuInputTest, IdentifiesCallbackAndSynchronousMissionGoalsSources)
{
	using AVPE::NativeMenuInput::Source;

	EXPECT_EQ(static_cast<u8>(AVPE::NativeMenuInput::Action::Activate), 4);
	EXPECT_EQ(AVPE::NativeMenuInput::IdentifyMenuSource(0x01500000, 0, 0), Source::CallbackRegistry);
	EXPECT_EQ(AVPE::NativeMenuInput::IdentifyMenuSource(0, 0x01510000, 0x00342570),
		Source::MissionGoalsLoad);
	EXPECT_EQ(AVPE::NativeMenuInput::IdentifyMenuSource(0, 0x01510000, 0x00342574), Source::None);
	EXPECT_EQ(AVPE::NativeMenuInput::IdentifyMenuSource(0, 0, 0x00342570), Source::None);
	EXPECT_STREQ(AVPE::NativeMenuInput::SourceName(Source::CallbackRegistry), "callback-registry");
	EXPECT_STREQ(AVPE::NativeMenuInput::SourceName(Source::MissionGoalsLoad), "mission-goals-load");
	EXPECT_STREQ(AVPE::NativeMenuInput::SourceName(Source::None), "none");
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

TEST(NativeBiosTraceTest, MissionTypeInitializerPairsReturnsByStackPointer)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);
	AVPE::NativeBiosTrace::ObserveMissionTypeInitializer(
		0x0017467C, 0x00200000, 0x01000000, 0x01100000, 4, 0x01FFF000, 0x1234, 0x5678, true);
	AVPE::NativeBiosTrace::ObserveMissionTypeInitializer(
		0x00174684, 0, 0, 0, 0, 0x01FFE000, 0, 0, false);
	AVPE::NativeBiosTrace::ObserveMissionTypeInitializer(
		0x0017467C, 0x00210000, 0x01010000, 0x01110000, 2, 0x01FFE000, 0x9ABC, 0xDEF0, true);
	AVPE::NativeBiosTrace::ObserveMissionTypeInitializer(
		0x00174684, 0, 0, 0, 0, 0x01FFE000, 0, 0, false);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_NE(snapshot.find("\"type_initializer_progress\":{\"calls\":2,\"returns\":1,\"active_depth\":1"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"max_depth\":2,\"sequence_errors\":0,\"invalid_descriptors\":0"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"active\":[{\"target\":2097152"), std::string::npos);
	EXPECT_NE(snapshot.find("\"last_completed\":{\"target\":2162688"), std::string::npos);
	EXPECT_NE(snapshot.find("\"last_return\":{\"pc\":1525380"), std::string::npos);
}

TEST(NativeBiosTraceTest, MissionTypeInitializerReportsInvalidDescriptor)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);
	AVPE::NativeBiosTrace::ObserveMissionTypeInitializer(
		0x0017467C, 0x00200000, 0x01000000, 0x01100000, 1, 0x01FFF000, 0, 0, false);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_NE(snapshot.find("\"invalid_descriptors\":1"), std::string::npos);
	EXPECT_NE(snapshot.find("\"descriptor_valid\":false"), std::string::npos);
}

TEST(NativeBiosTraceTest, MissionObjectFactoryPairsReturnsByStackPointer)
{
	AVPE::NativeBiosTrace::SetEnabled(true);
	AVPE::NativeBiosTrace::StartMissionBoundary();
	AVPE::NativeBiosTrace::ObserveMissionBoundary(0x0016F910);
	AVPE::NativeBiosTrace::ObserveMissionObjectFactory(
		0x00127EC0, 0x00200000, 0x01000000, 0x1111, 0x01200000, 0x01FFF000);
	AVPE::NativeBiosTrace::ObserveMissionObjectFactory(0x00127EC8, 0, 0, 0, 0, 0x01FFE000);
	AVPE::NativeBiosTrace::ObserveMissionObjectFactory(
		0x00127EC0, 0x00210000, 0x01010000, 0x2222, 0x01210000, 0x01FFE000);
	AVPE::NativeBiosTrace::ObserveMissionObjectFactory(0x00127EC8, 0, 0, 0, 0, 0x01FFE000);

	const std::string snapshot = AVPE::NativeBiosTrace::CaptureMissionBoundaryJson(
		std::chrono::milliseconds(1));

	EXPECT_NE(snapshot.find("\"object_factory_progress\":{\"calls\":2,\"returns\":1,\"active_depth\":1"),
		std::string::npos);
	EXPECT_NE(snapshot.find("\"max_depth\":2,\"sequence_errors\":0"), std::string::npos);
	EXPECT_NE(snapshot.find("\"active\":[{\"target\":2097152"), std::string::npos);
	EXPECT_NE(snapshot.find("\"last_completed\":{\"target\":2162688"), std::string::npos);
	EXPECT_NE(snapshot.find("\"last_return\":{\"pc\":1212104"), std::string::npos);
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
