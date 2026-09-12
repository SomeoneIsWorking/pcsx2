#include "pcsx2/Config.h"

#include <gtest/gtest.h>

namespace
{
	TEST(RuntimeConfigTest, RetainsExplicitDataPathAcrossSettingsReset)
	{
		Pcsx2Config previous;
		previous.CustomDataPath = "avpe-user-data";
		Pcsx2Config reloaded;

		reloaded.CopyRuntimeConfig(previous);

		EXPECT_EQ(reloaded.CustomDataPath, "avpe-user-data");
	}

	TEST(RuntimeConfigTest, DoesNotInventDataPathWhenUnset)
	{
		Pcsx2Config previous;
		Pcsx2Config reloaded;

		reloaded.CopyRuntimeConfig(previous);

		EXPECT_TRUE(reloaded.CustomDataPath.empty());
	}
} // namespace
