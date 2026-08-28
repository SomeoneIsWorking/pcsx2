#include "AVPE/NativeBiosTrace.h"

#include <gtest/gtest.h>

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
	AVPE::NativeBiosTrace::RecordEeSyscall(34, "StartThread", 5, 6, 7, 8);
	AVPE::NativeBiosTrace::RecordException("ee", 8, 0x1000, true);
	AVPE::NativeBiosTrace::RecordImport("ioman", 6, "read", 1, 2, 3, 4, -1, false, false);

	const std::string snapshot = AVPE::NativeBiosTrace::SnapshotJson();
	EXPECT_NE(snapshot.find("\"sequence\":1"), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"module\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"interrupt\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"rpc\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"ee_syscall\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"name\":\"StartThread\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"arguments\":[5,6,7,8]"), std::string::npos);
	EXPECT_NE(snapshot.find("\"kind\":\"exception\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"domain\":\"ee\""), std::string::npos);
	EXPECT_NE(snapshot.find("\"branch_delay\":true"), std::string::npos);
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
