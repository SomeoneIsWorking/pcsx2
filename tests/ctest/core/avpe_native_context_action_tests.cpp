#include "AVPE/NativeContextAction.h"

#include <gtest/gtest.h>

#include <map>
#include <utility>
#include <vector>

namespace
{
	using AVPE::NativeContextAction;
	namespace Callbacks = AVPE::NativeInputCallbacks;

	// The registry as the game leaves it each frame: the context buttons for what the cursor
	// is over, each with hotkey and focus slots bound to the pad's Circle.
	class NativeContextActionTest : public testing::Test
	{
	protected:
		static constexpr u32 device = 0x01450000;
		static constexpr u32 entries = 0x00A61000;
		static constexpr u32 attack = 0x014DA5B0;
		static constexpr u32 move = 0x01509CB0;
		std::map<u32, u32> words;
		std::map<u32, u32> handles;
		std::map<std::pair<u32, u32>, u32> members;
		std::vector<std::pair<u32, u32>> queued;
		bool idle = true;
		u32 registered = 0;
		NativeContextAction action;
		NativeContextAction::Guest guest;

		void SetUp() override
		{
			words[device + Callbacks::RegistryOffset] = entries;
			words[device + Callbacks::RegistryOffset + 8] = Callbacks::MaxCount;
			guest.read.word = [this](const u32 address, u32* value) { return Lookup(words, address, value); };
			guest.read.is_object = [this](const u32 address) { return words.contains(address); };
			guest.read.is_address = [](const u32 address) { return address != 0; };
			guest.read.handle = [this](const u32 handle, u32* value) { return Lookup(handles, handle, value); };
			guest.read.member = [this](const u32 owner, const u32 member, u32* value) {
				return Lookup(members, std::pair{owner, member}, value);
			};
			guest.dispatch_idle = [this] { return idle; };
			guest.queue = [this](const Callbacks::Target& target) {
				queued.emplace_back(target.function, target.object);
				return true;
			};
		}

		template <typename Key>
		static bool Lookup(const std::map<Key, u32>& values, const Key key, u32* value)
		{
			const auto found = values.find(key);
			if (found == values.end())
			{
				return false;
			}
			*value = found->second;
			return true;
		}

		void RegisterButton(const u32 button, const u32 vtable, const u32 focus, const u32 hotkey)
		{
			const u32 handle = (button & 0xFFFF0) << 4;
			words[button] = vtable;
			handles[handle] = button;
			for (const u32 function : {hotkey, focus})
			{
				const u32 callback = entries + registered++ * Callbacks::Stride;
				words[callback + Callbacks::OwnerOffset] = handle;
				members[{button, callback + Callbacks::MemberOffset}] = function;
			}
			words[device + Callbacks::RegistryOffset + 4] = registered;
		}

		void RegisterAttack()
		{
			RegisterButton(attack, NativeContextAction::DwimButtonVtable, Callbacks::MenuItemFocusKeyActivate,
				Callbacks::MenuItemHotKeyActivate);
		}

		void RegisterMove()
		{
			RegisterButton(move, NativeContextAction::MoveDwimButtonVtable,
				NativeContextAction::MoveDwimFocusKeyActivate, NativeContextAction::MoveDwimHotKeyActivate);
		}

		void Click()
		{
			ASSERT_TRUE(action.Press());
			action.Step(device, guest);
			ASSERT_TRUE(action.Release());
			action.Step(device, guest);
		}
	};

	TEST_F(NativeContextActionTest, OverAnEnemyPressFocusesAndReleaseFiresTheAttackButton)
	{
		RegisterAttack();
		Click();
		const std::vector<std::pair<u32, u32>> expected{
			{Callbacks::MenuItemFocusKeyActivate, attack}, {Callbacks::MenuItemHotKeyActivate, attack}};
		EXPECT_EQ(queued, expected);
	}

	TEST_F(NativeContextActionTest, OverGroundItRunsTheMoveButton)
	{
		RegisterMove();
		Click();
		const std::vector<std::pair<u32, u32>> expected{
			{NativeContextAction::MoveDwimFocusKeyActivate, move}, {NativeContextAction::MoveDwimHotKeyActivate, move}};
		EXPECT_EQ(queued, expected);
	}

	TEST_F(NativeContextActionTest, WithoutAContextButtonNothingRuns)
	{
		Click();
		EXPECT_TRUE(queued.empty());
	}

	TEST_F(NativeContextActionTest, EachEdgeWaitsForAnIdleDispatch)
	{
		RegisterMove();
		idle = false;
		ASSERT_TRUE(action.Press());
		ASSERT_TRUE(action.Release());
		action.Step(device, guest);
		EXPECT_TRUE(queued.empty());
		idle = true;
		action.Step(device, guest);
		ASSERT_EQ(queued.size(), 1U);
		EXPECT_EQ(queued[0].first, NativeContextAction::MoveDwimFocusKeyActivate);
		action.Step(device, guest);
		ASSERT_EQ(queued.size(), 2U);
		EXPECT_EQ(queued[1].first, NativeContextAction::MoveDwimHotKeyActivate);
	}

	TEST_F(NativeContextActionTest, TheReleaseEndsTheButtonThePressFocused)
	{
		RegisterMove();
		ASSERT_TRUE(action.Press());
		action.Step(device, guest);
		RegisterAttack();
		ASSERT_TRUE(action.Release());
		action.Step(device, guest);
		ASSERT_EQ(queued.size(), 2U);
		EXPECT_EQ(queued[1], std::pair(NativeContextAction::MoveDwimHotKeyActivate, move));
	}

	TEST_F(NativeContextActionTest, AReleaseWithoutAFocusedButtonRunsNothing)
	{
		RegisterAttack();
		ASSERT_TRUE(action.Release());
		action.Step(device, guest);
		EXPECT_TRUE(queued.empty());
	}

	TEST_F(NativeContextActionTest, PendingEdgesAreBounded)
	{
		for (int edge = 0; edge < 4; ++edge)
		{
			ASSERT_TRUE(edge % 2 == 0 ? action.Press() : action.Release());
		}
		EXPECT_FALSE(action.Press());
		action.Reset();
		EXPECT_TRUE(action.Press());
	}
} // namespace
