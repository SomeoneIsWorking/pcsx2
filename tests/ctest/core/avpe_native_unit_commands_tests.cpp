#include "AVPE/NativeUnitCommands.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string_view>
#include <vector>

namespace
{
	using AVPE::NativeUnitCommands;

	class NativeUnitCommandsTest : public testing::Test
	{
	protected:
		static constexpr u32 device = 0x00900000;
		static constexpr u32 callbacks = 0x00901000;
		static constexpr u32 in_game = 0x01000000;
		static constexpr u32 toggle = 0x01001000;
		static constexpr u32 card = 0x01002000;
		static constexpr u32 aggressive = 0x01003000;
		static constexpr u32 waypoint = 0x01004000;
		static constexpr u32 pointer = 0x01005000;
		static constexpr u32 selection = 0x01006000;
		static constexpr u32 unit_menu = 0x01007000;
		static constexpr u32 group_two = 0x0100A000;
		static constexpr u32 grouping = 0x0100B000;
		static constexpr u32 event = 0x0100C000;
		static constexpr u32 hotkey_activate = 0x00120F40;

		std::map<u32, u32> words;
		std::map<u32, u32> handles;
		std::map<std::pair<u32, u32>, u32> members;
		std::vector<AVPE::NativeInputCallbacks::Target> queued;
		bool idle = true;
		u32 registered = 0;
		NativeUnitCommands commands;
		NativeUnitCommands::Guest guest;

		void SetUp() override
		{
			words[NativeUnitCommands::InGameMenuPointer] = in_game;
			words[in_game] = 0x0035BDC0;
			words[in_game + 0x290] = 0;
			words[in_game + NativeUnitCommands::CurrentMenuOffset] = unit_menu;
			words[toggle] = NativeUnitCommands::ToggleMenuButtonVtable;
			handles[0x10000] = toggle;
			words[NativeUnitCommands::PointerInstance] = pointer;
			words[pointer] = 0x00338420;
			words[pointer + NativeUnitCommands::SelectionOffset] = selection;
			words[selection + 4] = 1;
			words[device + NativeUnitCommands::CallbackArrayOffset] = callbacks;
			// The pointer and HUD also register; not every owner resolves as a plausible object.
			Register(0x01FFF000, 0x70000, 0x00106DA0);
			Register(toggle, 0x10000, NativeUnitCommands::ToggleOpenFunction);
			Register(toggle, 0x10000, NativeUnitCommands::ToggleCloseFunction);

			words[card] = 0x0035BBA0;
			words[card + 8] = aggressive;
			AddItem(aggressive, 0x20000, waypoint, 0x01008000, "Aggressive");
			AddItem(waypoint, 0x30000, 0, 0x01009000, "Waypoint");

			guest.read.word = [this](const u32 address, u32* value) { return Lookup(words, address, value); };
			guest.read.is_object = [this](const u32 address) { return words.contains(address); };
			guest.read.handle = [this](const u32 handle, u32* value) { return Lookup(handles, handle, value); };
			guest.read.member = [this](const u32 owner, const u32 member, u32* value) {
				return Lookup(members, std::pair{owner, member}, value);
			};
			guest.dispatch_idle = [this] { return idle; };
			guest.queue = [this](const AVPE::NativeInputCallbacks::Target& target) {
				queued.push_back(target);
				return true;
			};
		}

		template <typename Key>
		static bool Lookup(const std::map<Key, u32>& values, const Key key, u32* value)
		{
			const auto found = values.find(key);
			if (found == values.end())
				return false;
			*value = found->second;
			return true;
		}

		void Register(const u32 owner, const u32 handle, const u32 function)
		{
			const u32 callback = callbacks + registered * 0x18;
			words[callback + 8] = handle;
			words[callback + 0x14] = 0;
			members[{owner, callback + 0x0C}] = function;
			words[device + NativeUnitCommands::CallbackArrayOffset + 4] = ++registered;
		}

		void AddItem(const u32 item, const u32 handle, const u32 sibling, const u32 text, const std::string_view label)
		{
			words[item] = 0x0035BAA0;
			words[item + 8] = 0;
			words[item + 0x10] = sibling;
			words[item + 0x18] = handle;
			words[item + 0x110] = 0;
			words[item + 0x148] = text;
			handles[handle] = item;
			for (u32 offset = 0; offset <= label.size(); offset += 4)
			{
				u32 word = 0;
				std::memcpy(&word, label.data() + offset, std::min<size_t>(4, label.size() - offset));
				words[text + offset] = word;
			}
		}

		// R2's callback shows the card in place of the unit's menu and registers its items.
		void GuestShowsCard()
		{
			words[in_game + 0x290] = 0x01000000;
			words[in_game + NativeUnitCommands::CurrentMenuOffset] = card;
			Register(aggressive, 0x20000, hotkey_activate);
			Register(waypoint, 0x30000, hotkey_activate);
		}

		// The unit's own menu: group 2 on d-pad right, the event jump, and L2 grouping.
		void GuestShowsUnitMenu()
		{
			words[unit_menu] = 0x003456D0;
			words[unit_menu + 8] = group_two;
			AddItem(group_two, 0x40000, event, 0x0100D000, "");
			words[group_two + 0x118] = NativeUnitCommands::GroupHotkeys[1];
			AddItem(event, 0x60000, 0, 0x0100E000, "");
			words[event + 0x118] = NativeUnitCommands::EventHotkey;
			words[grouping] = NativeUnitCommands::GroupingButtonVtable;
			handles[0x50000] = grouping;
			Register(grouping, 0x50000, NativeUnitCommands::GroupingPressFunction);
			Register(grouping, 0x50000, NativeUnitCommands::GroupingReleaseFunction);
			Register(group_two, 0x40000, hotkey_activate);
			Register(event, 0x60000, hotkey_activate);
		}

		void Step() { commands.Step(device, guest); }
	};

	TEST_F(NativeUnitCommandsTest, ALetterOpensTheCardFiresItsOrderAndClosesIt)
	{
		ASSERT_TRUE(commands.CardOrder('W'));
		EXPECT_FALSE(commands.CardOrder('A'));
		Step();
		ASSERT_EQ(queued.size(), 1u);
		EXPECT_EQ(queued[0].object, toggle);
		EXPECT_EQ(queued[0].function, NativeUnitCommands::ToggleOpenFunction);

		idle = false;
		Step();
		EXPECT_EQ(queued.size(), 1u);
		idle = true;
		GuestShowsCard();
		Step();
		ASSERT_EQ(queued.size(), 2u);
		EXPECT_EQ(queued[1].object, waypoint);
		EXPECT_EQ(queued[1].function, hotkey_activate);

		Step();
		ASSERT_EQ(queued.size(), 3u);
		EXPECT_EQ(queued[2].object, toggle);
		EXPECT_EQ(queued[2].function, NativeUnitCommands::ToggleCloseFunction);
		Step();
		EXPECT_EQ(queued.size(), 3u);
		EXPECT_TRUE(commands.CardOrder('A'));
	}

	TEST_F(NativeUnitCommandsTest, AShownCardTakesTheOrderAndStaysShown)
	{
		ASSERT_TRUE(commands.ShowCard(true));
		Step();
		ASSERT_EQ(queued.size(), 1u);
		GuestShowsCard();
		ASSERT_TRUE(commands.CardOrder('A'));
		Step();
		ASSERT_EQ(queued.size(), 2u);
		EXPECT_EQ(queued[1].object, aggressive);
		Step();
		EXPECT_EQ(queued.size(), 2u);
		ASSERT_TRUE(commands.ShowCard(false));
		Step();
		ASSERT_EQ(queued.size(), 3u);
		EXPECT_EQ(queued[2].function, NativeUnitCommands::ToggleCloseFunction);
	}

	TEST_F(NativeUnitCommandsTest, NoSelectionOrNoOrderLeavesTheCardAsItWas)
	{
		words[selection + 4] = 0;
		ASSERT_TRUE(commands.CardOrder('W'));
		Step();
		EXPECT_TRUE(queued.empty());

		words[selection + 4] = 1;
		ASSERT_TRUE(commands.CardOrder('Q'));
		Step();
		GuestShowsCard();
		Step();
		ASSERT_EQ(queued.size(), 2u);
		EXPECT_EQ(queued[1].function, NativeUnitCommands::ToggleCloseFunction);
	}

	TEST_F(NativeUnitCommandsTest, ARefusedOpenFiresNothing)
	{
		ASSERT_TRUE(commands.CardOrder('W'));
		Step();
		Step();
		EXPECT_EQ(queued.size(), 1u);
		EXPECT_TRUE(commands.CardOrder('W'));
	}

	TEST_F(NativeUnitCommandsTest, OutsideAMissionNothingIsQueued)
	{
		words.erase(NativeUnitCommands::InGameMenuPointer);
		ASSERT_TRUE(commands.CardOrder('W'));
		Step();
		EXPECT_TRUE(queued.empty());
		EXPECT_TRUE(commands.CardOrder('W'));
	}

	TEST_F(NativeUnitCommandsTest, ANumberRecallsItsGroupThroughTheUnitMenu)
	{
		GuestShowsUnitMenu();
		ASSERT_TRUE(commands.RecallGroup(1));
		Step();
		ASSERT_EQ(queued.size(), 1u);
		EXPECT_EQ(queued[0].object, group_two);
		EXPECT_EQ(queued[0].function, hotkey_activate);
		Step();
		EXPECT_EQ(queued.size(), 1u);
		EXPECT_FALSE(commands.RecallGroup(4));
	}

	TEST_F(NativeUnitCommandsTest, AssigningHoldsGroupingAroundTheGroupItem)
	{
		GuestShowsUnitMenu();
		ASSERT_TRUE(commands.AssignGroup(1));
		for (int frame = 0; frame < 4; frame++)
			Step();
		ASSERT_EQ(queued.size(), 3u);
		EXPECT_EQ(queued[0].function, NativeUnitCommands::GroupingPressFunction);
		EXPECT_EQ(queued[1].object, group_two);
		EXPECT_EQ(queued[2].function, NativeUnitCommands::GroupingReleaseFunction);
	}

	TEST_F(NativeUnitCommandsTest, UnitMenuKeysDoNothingWhileTheCardIsShown)
	{
		GuestShowsUnitMenu();
		GuestShowsCard();
		words[aggressive + 0x118] = NativeUnitCommands::GroupHotkeys[1];
		ASSERT_TRUE(commands.RecallGroup(1));
		Step();
		ASSERT_TRUE(commands.AssignGroup(1));
		Step();
		EXPECT_TRUE(queued.empty());
	}

	TEST_F(NativeUnitCommandsTest, SpaceAndQFireTheirUnitMenuItems)
	{
		GuestShowsUnitMenu();
		ASSERT_TRUE(commands.JumpToEvent());
		Step();
		ASSERT_EQ(queued.size(), 1u);
		EXPECT_EQ(queued[0].object, event);
		ASSERT_TRUE(commands.JumpToBase());
		Step();
		EXPECT_EQ(queued.size(), 1u);
		words[event + 0x118] = NativeUnitCommands::SpecialHotkey;
		ASSERT_TRUE(commands.UseSpecial());
		Step();
		ASSERT_EQ(queued.size(), 2u);
		EXPECT_EQ(queued[1].object, event);
	}
} // namespace
