#include "AVPE/NativeMenuItems.h"
#include "AVPE/NativeAttractInput.h"

#include <gtest/gtest.h>

#include <map>

namespace
{
	using AVPE::NativeMenuItems::Status;

	class NativeMenuItemsTest : public testing::Test
	{
	protected:
		static constexpr u32 menu = 0x01000000;
		static constexpr u32 first = 0x01001000;
		static constexpr u32 second = 0x01002000;
		static constexpr u32 callbacks = 0x01003000;
		static constexpr u32 hotkey = 0x00120F40;
		std::map<u32, u32> words;
		std::map<u32, u32> handles;
		std::map<std::pair<u32, u32>, u32> members;
		AVPE::NativeInputCallbacks::Access access;
		AVPE::NativeInputCallbacks::Target target;
		const char* error = "";

		void SetUp() override
		{
			words[menu + 8] = first;
			AddItem(first, 1, second);
			AddItem(second, 2, 0);
			AddCallback(0, first, 1);
			AddCallback(1, second, 2);
			access.word = [this](const u32 address, u32* value) { return Lookup(words, address, value); };
			access.is_object = [this](const u32 address) { return words.contains(address); };
			access.handle = [this](const u32 handle, u32* value) { return Lookup(handles, handle, value); };
			access.member = [this](const u32 owner, const u32 member, u32* value) {
				return Lookup(members, std::pair{owner, member}, value);
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

		void AddItem(const u32 object, const u32 handle, const u32 sibling)
		{
			words[object] = 0x00331610;
			words[object + 8] = 0;
			words[object + 0x10] = sibling;
			words[object + 0x18] = handle;
			words[object + 0x110] = 0x807F1E5F;
			handles[handle] = object;
		}

		void AddCallback(const u32 index, const u32 owner, const u32 handle, const u32 function = hotkey)
		{
			const u32 callback = callbacks + index * 0x18;
			words[callback + 8] = handle;
			words[callback + 0x14] = function;
			members[{owner, callback + 0xC}] = function;
		}

		Status Find(const u32 focused, const u32 count = 2)
		{
			return AVPE::NativeMenuItems::FindActivationCallback(
				callbacks, count, menu, focused, &target, &error, access);
		}
	};

	TEST_F(NativeMenuItemsTest, SelectsOnlyCurrentFocusedRegisteredDescendant)
	{
		EXPECT_EQ(Find(first), Status::Success);
		EXPECT_EQ(target.object, first);
		EXPECT_EQ(target.callback, callbacks);
		EXPECT_EQ(target.function, hotkey);
		EXPECT_EQ(Find(second), Status::Success);
		EXPECT_EQ(target.object, second);
		EXPECT_EQ(target.callback, callbacks + 0x18);
		EXPECT_EQ(Find(0), Status::FocusUnavailable);
		EXPECT_EQ(Find(menu), Status::FocusUnavailable);
	}

	TEST_F(NativeMenuItemsTest, PreservesActivateFocusedHotkeyPrecedenceAndAmbiguity)
	{
		words[second + 0x110] = 0x21383159;
		EXPECT_EQ(Find(first), Status::Success);
		EXPECT_EQ(target.object, second);
		words[first + 0x110] = 0x21383159;
		EXPECT_EQ(Find(first), Status::AmbiguousMenu);
	}

	TEST_F(NativeMenuItemsTest, CoalescesEquivalentPhysicalBindingsForTheSameItem)
	{
		AddCallback(1, first, 1);
		EXPECT_EQ(Find(first), Status::Success);
		EXPECT_EQ(target.object, first);
		EXPECT_EQ(target.callback, callbacks);
	}

	TEST_F(NativeMenuItemsTest, NeverBypassesRegisteredTitleAttractCancellation)
	{
		constexpr u32 attract = 0x01765D10;
		handles[3] = attract;
		words[attract] = AVPE::GuestObjects::AttractExitVtable;
		AddCallback(2, attract, 3, 0x00206A60);
		EXPECT_EQ(Find(first, 3), Status::AmbiguousMenu);
		AddCallback(3, attract, 3, 0x002069E0);
		words[second + 0x110] = 0x21383159;
		EXPECT_EQ(Find(first, 4), Status::AmbiguousMenu);
		// The title removes the attract registration through its own lifecycle.
		EXPECT_EQ(Find(first, 2), Status::Success);
		EXPECT_EQ(target.object, second);
	}

	TEST_F(NativeMenuItemsTest, PreservesUnrelatedExpiredRegistryEntries)
	{
		AddCallback(2, 0x01800000, 99);
		words[callbacks + 2 * 0x18 + 0x14] = 0x00125230;
		EXPECT_EQ(Find(first, 3), Status::Success);
		EXPECT_EQ(target.object, first);
		words[callbacks + 2 * 0x18 + 0x14] = 0x00206A60;
		EXPECT_EQ(Find(first, 3), Status::GuestMemoryError);
	}

	TEST_F(NativeMenuItemsTest, RejectsUnregisteredOutsideAndWrongFunctionTargets)
	{
		EXPECT_EQ(Find(first, 0), Status::FocusUnavailable);
		members[{first, callbacks + 0xC}] = 0x00125230;
		EXPECT_EQ(Find(first), Status::FocusUnavailable);
		members.erase({first, callbacks + 0xC});
		EXPECT_EQ(Find(first), Status::GuestMemoryError);
		words[menu + 8] = second;
		EXPECT_EQ(Find(first), Status::FocusUnavailable);
	}

	TEST_F(NativeMenuItemsTest, RejectsUnreadableTreeRegistryAndMismatchedHandles)
	{
		const auto valid_words = words;
		for (const u32 address : {menu + 8, first + 8, first + 0x10,
				 first + 0x18, first + 0x110, callbacks + 8})
		{
			words.erase(address);
			EXPECT_EQ(Find(first), Status::GuestMemoryError) << address;
			words = valid_words;
		}
		handles[1] = second;
		EXPECT_EQ(Find(first), Status::GuestMemoryError);
		EXPECT_EQ(Find(first, 257), Status::GuestMemoryError);
	}

	TEST_F(NativeMenuItemsTest, PreservesExactUniqueMissionGoalsExitDiscovery)
	{
		u32 exit = 0;
		EXPECT_EQ(AVPE::NativeMenuItems::FindMissionGoalsExitItem(menu, &exit, &error, access),
			Status::FocusUnavailable);
		words[first] = 0x00342370;
		EXPECT_EQ(AVPE::NativeMenuItems::FindMissionGoalsExitItem(menu, &exit, &error, access),
			Status::Success);
		EXPECT_EQ(exit, first);
		words[second] = 0x00342370;
		EXPECT_EQ(AVPE::NativeMenuItems::FindMissionGoalsExitItem(menu, &exit, &error, access),
			Status::AmbiguousMenu);
	}

	TEST_F(NativeMenuItemsTest, EnforcesExactDescendantBound)
	{
		constexpr u32 start = 0x01200000;
		words[menu + 8] = start;
		for (u32 index = 0; index < 256; ++index)
			AddItem(start + index * 0x1000, index + 1, index == 255 ? 0 : start + (index + 1) * 0x1000);
		AddCallback(0, start, 1);
		EXPECT_EQ(Find(start, 1), Status::Success);
		words[start + 255 * 0x1000 + 0x10] = start + 256 * 0x1000;
		AddItem(start + 256 * 0x1000, 257, 0);
		EXPECT_EQ(Find(start, 1), Status::GuestMemoryError);
	}

	class NativeAttractInputTest : public NativeMenuItemsTest
	{
	protected:
		static constexpr u32 attract = 0x01765D10;
		static constexpr u32 button_input = 0x00206A60;
		static constexpr u32 analog_input = 0x002069E0;

		void SetUp() override
		{
			NativeMenuItemsTest::SetUp();
			handles[3] = attract;
			words[attract] = AVPE::GuestObjects::AttractExitVtable;
			AddCallback(2, attract, 3, analog_input);
			AddCallback(3, attract, 3, button_input);
		}

		AVPE::NativeAttractInput::Result Cancel(const u32 count = 4)
		{
			return AVPE::NativeAttractInput::FindCancellation(callbacks, count, access);
		}
	};

	TEST_F(NativeAttractInputTest, SelectsButtonOnlyAndCoalescesSameOwnerAliasesWithoutAMenu)
	{
		words.erase(menu + 8);
		AddCallback(4, attract, 3, button_input);
		const auto result = Cancel(5);
		EXPECT_EQ(result.status, AVPE::NativeAttractInput::Status::Available);
		EXPECT_EQ(result.target.object, attract);
		EXPECT_EQ(result.target.function, button_input);
		EXPECT_EQ(result.target.callback, callbacks + 3 * 0x18);
	}

	TEST_F(NativeAttractInputTest, AbsentAndAnalogOnlyAreDifferentResults)
	{
		EXPECT_EQ(Cancel(2).status, AVPE::NativeAttractInput::Status::Absent);
		const auto result = Cancel(3);
		EXPECT_EQ(result.status, AVPE::NativeAttractInput::Status::Invalid);
		EXPECT_EQ(result.target.object, 0u);
		EXPECT_NE(result.error[0], char{});
	}

	TEST_F(NativeAttractInputTest, RejectsInvalidOwnersDescriptorsAndUnreadableRegistry)
	{
		const auto valid_words = words;
		for (const u32 address : {callbacks + 0x14, callbacks + 2 * 0x18 + 8, attract})
		{
			words.erase(address);
			EXPECT_EQ(Cancel().status, AVPE::NativeAttractInput::Status::Invalid) << address;
			words = valid_words;
		}
		members[{attract, callbacks + 3 * 0x18 + 0xC}] = hotkey;
		EXPECT_EQ(Cancel().status, AVPE::NativeAttractInput::Status::Invalid);
		EXPECT_EQ(Cancel(257).status, AVPE::NativeAttractInput::Status::Invalid);
	}

	TEST_F(NativeAttractInputTest, RejectsCompetingOwnersAndIgnoresUnrelatedExpiredEntries)
	{
		AddCallback(4, 0x01800000, 99, hotkey);
		EXPECT_EQ(Cancel(5).status, AVPE::NativeAttractInput::Status::Available);
		handles[4] = 0x01800000;
		words[0x01800000] = AVPE::GuestObjects::AttractExitVtable;
		AddCallback(4, 0x01800000, 4, button_input);
		EXPECT_EQ(Cancel(5).status, AVPE::NativeAttractInput::Status::Ambiguous);
	}
} // namespace
