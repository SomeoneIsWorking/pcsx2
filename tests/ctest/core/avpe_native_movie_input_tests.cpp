#include "AVPE/NativeMovieInput.h"
#include "AVPE/NativeMenuRoute.h"

#include <gtest/gtest.h>

#include <map>
#include <limits>

namespace
{
	using namespace AVPE::NativeMovieInput;
	constexpr u32 Player = 0x004251F0;
	constexpr u32 Decoder = 0x0119B160;

	class NativeMovieInputTest : public testing::Test
	{
	protected:
		std::map<u32, u32> words{
			{0x003672BC, Player}, {Player, 0x00334420}, {Player + 0x3C, 0},
			{0x0036741C, Decoder}, {Decoder + 8, 11}, {Decoder + 0xA8, 0}};
		Cancellation cancellation{[this](u32 address, u32* value) {
			const auto found = words.find(address);
			if (found == words.end())
				return false;
			*value = found->second;
			return true;
		}};
		u32 calls = 0;
		AVPE::EECallShuttle::Request last_request;

		void Service()
		{
			cancellation.Service([this](const AVPE::EECallShuttle::Request& request) {
				++calls;
				last_request = request;
				return AVPE::EECallShuttle::DeferredTicket{.status = AVPE::EECallShuttle::Status::Success, .id = 42};
			});
		}
	};

	TEST_F(NativeMovieInputTest, AbsentPlayerLeavesMenuInputAvailable)
	{
		words[0x003672BC] = 0;
		EXPECT_EQ(cancellation.Request().admission, Admission::Absent);
		Service();
		EXPECT_EQ(calls, 0U);
		EXPECT_EQ(cancellation.Inspect().state, State::Idle);
	}

	TEST_F(NativeMovieInputTest, DeferredContextWaitsForUserCodeAndMainRamStack)
	{
		using AVPE::EECallShuttle::CanInstallDeferredContext;
		EXPECT_TRUE(CanInstallDeferredContext(0x00181038, 0x01143790, false));
		EXPECT_TRUE(CanInstallDeferredContext(0x002AC944, 0x01197510, false));
		EXPECT_FALSE(CanInstallDeferredContext(0x80003B38, 0x01197510, false));
		EXPECT_FALSE(CanInstallDeferredContext(0x00181038, 0x80001000, false));
		EXPECT_FALSE(CanInstallDeferredContext(0x00181038, 0x01143790, true));
		EXPECT_FALSE(CanInstallDeferredContext(0x00181039, 0x01143790, false));
		EXPECT_FALSE(CanInstallDeferredContext(0x00181038, 0x01143794, false));
		EXPECT_FALSE(CanInstallDeferredContext(0x00181038, 0x30, false));
		const AVPE::EECallShuttle::Request movie{.caller_begin = 0x00180D70, .caller_end = 0x00180F40};
		EXPECT_TRUE(CanInstallDeferredContext(0x00180DF8, 0x01197510, false, movie));
		EXPECT_FALSE(CanInstallDeferredContext(0x00181038, 0x01197510, false, movie));
		EXPECT_FALSE(CanInstallDeferredContext(0x00180F40, 0x01197510, false, movie));
	}

	TEST_F(NativeMovieInputTest, AdmissionResponsePreservesFullWidthTicketsAndPendingState)
	{
		AVPE::NativeMenuInput::Result result;
		result.source = AVPE::NativeMenuInput::Source::MovieCancellation;
		result.awaiting_readiness = true;
		result.elapsed_cycles = std::numeric_limits<u64>::max();
		result.movie_action_id = std::numeric_limits<u64>::max();
		result.dispatch_action_id = std::numeric_limits<u64>::max();
		result.deferred_call_id = std::numeric_limits<u64>::max();
		result.readiness_action_id = std::numeric_limits<u64>::max();
		const std::string response = AVPE::NativeMenuRoute::FormatActionResponse("activate", result);
		EXPECT_NE(response.find(R"("movie_action_id":18446744073709551615)"), std::string::npos);
		EXPECT_NE(response.find(R"("execution":"pending")"), std::string::npos);
		EXPECT_TRUE(response.ends_with(R"("awaiting_readiness":true})"));
	}

	TEST_F(NativeMovieInputTest, InvalidAndNonSkippablePlayersRefuse)
	{
		words[Player] = 0x00342A50;
		EXPECT_EQ(cancellation.Request().admission, Admission::Rejected);
		words[Player] = 0x00334420;
		words[Player + 0x3C] = 1;
		EXPECT_EQ(cancellation.Request().admission, Admission::Rejected);
		words.erase(Player + 0x3C);
		EXPECT_EQ(cancellation.Request().admission, Admission::Rejected);
		Service();
		EXPECT_EQ(calls, 0U);
	}

	TEST_F(NativeMovieInputTest, EarlyPressWaitsForLiveLoopAndOriginalFrameThreshold)
	{
		words[Decoder + 8] = 10;
		const auto request = cancellation.Request();
		ASSERT_EQ(request.admission, Admission::Accepted);
		ASSERT_NE(request.action.id, 0U);
		Service();
		EXPECT_EQ(calls, 0U);
		cancellation.ObserveLoop(Decoder);
		Service();
		EXPECT_EQ(calls, 0U);
		words[Decoder + 8] = 0xFFFFFFFF;
		Service();
		EXPECT_EQ(calls, 0U); // The original readiness comparison is signed.
		EXPECT_EQ(cancellation.Request().admission, Admission::Rejected);
		words[Decoder + 8] = 11;
		const auto before = words;
		Service();
		EXPECT_EQ(calls, 1U);
		EXPECT_EQ(last_request.function, 0x00183C20U);
		EXPECT_EQ(last_request.caller_begin, 0x00180D70U);
		EXPECT_EQ(last_request.caller_end, 0x00180F40U);
		EXPECT_EQ(last_request.arguments, (std::array<u64, 4>{Decoder, 0, 0, 0}));
		EXPECT_EQ(words, before);
		EXPECT_EQ(cancellation.Inspect().state, State::Dispatched);
		EXPECT_EQ(cancellation.Inspect().deferred_call_id, 42U);
		ASSERT_TRUE(last_request.can_execute);
		EXPECT_TRUE(last_request.can_execute());
		EXPECT_EQ(cancellation.Request().admission, Admission::Rejected);
		Service();
		EXPECT_EQ(calls, 1U);
		cancellation.EndPlayback();
		EXPECT_EQ(cancellation.Inspect().state, State::Dispatched);
		cancellation.EndPlayer();
		EXPECT_EQ(cancellation.Inspect().state, State::PlayerExited);
		EXPECT_FALSE(last_request.can_execute());
	}

	TEST_F(NativeMovieInputTest, DecoderIdentityMismatchFailsWithoutDispatchOrRetry)
	{
		ASSERT_EQ(cancellation.Request().admission, Admission::Accepted);
		cancellation.ObserveLoop(Decoder + 4);
		Service();
		EXPECT_EQ(cancellation.Inspect().state, State::Failed);
		EXPECT_STRNE(cancellation.Inspect().error, "");
		cancellation.ObserveLoop(Decoder);
		Service();
		EXPECT_EQ(calls, 0U);
	}

	TEST_F(NativeMovieInputTest, AlreadyAbortingDecoderConsumesNoFurtherAction)
	{
		ASSERT_EQ(cancellation.Request().admission, Admission::Accepted);
		cancellation.ObserveLoop(Decoder);
		words[Decoder + 0xA8] = 1;
		Service();
		EXPECT_EQ(cancellation.Inspect().state, State::Expired);
		EXPECT_EQ(calls, 0U);
	}

	TEST_F(NativeMovieInputTest, EndOfPlaybackExpiresPendingPressBeforeCleanup)
	{
		ASSERT_EQ(cancellation.Request().admission, Admission::Accepted);
		cancellation.ObserveLoop(Decoder);
		cancellation.EndPlayback();
		words.erase(Decoder + 8);
		Service();
		EXPECT_EQ(cancellation.Inspect().state, State::Expired);
		EXPECT_EQ(cancellation.Request().admission, Admission::Rejected);
		EXPECT_EQ(calls, 0U);
	}

	TEST_F(NativeMovieInputTest, ReusedPlayerAddressCannotInheritPendingPress)
	{
		const auto first = cancellation.Request();
		ASSERT_EQ(first.admission, Admission::Accepted);
		cancellation.EndPlayer();
		cancellation.ObserveLoop(Decoder);
		Service();
		EXPECT_EQ(calls, 0U);
		const auto second = cancellation.Request();
		EXPECT_EQ(second.admission, Admission::Accepted);
		EXPECT_GT(second.action.id, first.action.id);
		Service();
		EXPECT_EQ(calls, 1U);
		cancellation.EndPlayer();
		EXPECT_EQ(cancellation.Request().admission, Admission::Accepted);
	}

	TEST_F(NativeMovieInputTest, OwnerLossAndResetDoNotLeakInput)
	{
		const auto first = cancellation.Request();
		ASSERT_EQ(first.admission, Admission::Accepted);
		cancellation.ObserveLoop(Decoder);
		words[0x003672BC] = 0;
		Service();
		EXPECT_EQ(cancellation.Inspect().state, State::Expired);
		words[0x003672BC] = Player;
		ASSERT_EQ(cancellation.Request().admission, Admission::Accepted);
		cancellation.Reset();
		Service();
		EXPECT_EQ(calls, 0U);
		EXPECT_EQ(cancellation.Inspect().state, State::Idle);
		EXPECT_GT(cancellation.Request().action.id, first.action.id);
		Service();
		EXPECT_EQ(calls, 0U); // Reset requires a fresh loop observation.
	}

	TEST_F(NativeMovieInputTest, DispatchFailureIsTerminalAndPreservesReason)
	{
		ASSERT_EQ(cancellation.Request().admission, Admission::Accepted);
		cancellation.ObserveLoop(Decoder);
		cancellation.Service([](const AVPE::EECallShuttle::Request&) {
			return AVPE::EECallShuttle::DeferredTicket{.status = AVPE::EECallShuttle::Status::Busy, .error = "busy"};
		});
		EXPECT_EQ(cancellation.Inspect().state, State::Failed);
		EXPECT_STREQ(cancellation.Inspect().error, "busy");
		Service();
		EXPECT_EQ(calls, 0U);
	}

	TEST_F(NativeMovieInputTest, TeardownInvalidatesAQueuedGuestCallBeforeLaterPlayerReuse)
	{
		ASSERT_EQ(cancellation.Request().admission, Admission::Accepted);
		cancellation.ObserveLoop(Decoder);
		Service();
		ASSERT_EQ(cancellation.Inspect().state, State::Dispatched);
		cancellation.EndPlayback(true);
		EXPECT_EQ(cancellation.Inspect().state, State::Expired);
		cancellation.EndPlayer();
		cancellation.ObserveLoop(Decoder);
		Service();
		EXPECT_EQ(calls, 1U);
	}

	TEST_F(NativeMovieInputTest, DeferredInstallationRevalidatesDecoderBeforeAnyGuestExecution)
	{
		ASSERT_EQ(cancellation.Request().admission, Admission::Accepted);
		cancellation.ObserveLoop(Decoder);
		Service();
		ASSERT_TRUE(last_request.can_execute());
		words[Decoder + 0xA8] = 3;
		EXPECT_FALSE(last_request.can_execute());
		words[Decoder + 0xA8] = 0;
		words[Player + 0x3C] = 1;
		EXPECT_FALSE(last_request.can_execute());
		cancellation.EndPlayer(true);
		words.erase(Decoder + 8);
		EXPECT_FALSE(last_request.can_execute());
	}
} // namespace
