#include "AVPE/AVPE.h"

#include <gtest/gtest.h>

namespace
{
	TEST(ButtonInjectionTest, PreservesInputAndTranslatedWireMasks)
	{
		AVPE::PressButtons(1u << 9, 1000);

		const AVPE::ButtonInjectionState state = AVPE::ActiveButtonInjection();
		EXPECT_EQ(state.inputs_mask, 1u << 9);
		EXPECT_EQ(state.wire_mask, 1u << 11);

		AVPE::PressButtons(1u << 9, 0);
		EXPECT_EQ(AVPE::ActiveButtonInjection().inputs_mask, 0u);
		EXPECT_EQ(AVPE::ActiveButtonInjection().wire_mask, 0u);
	}
} // namespace
