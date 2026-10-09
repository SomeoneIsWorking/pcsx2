#include "AVPE/NativeInputCallbacks.h"
#include "AVPE/NativeMenuInput.h"

#include <gtest/gtest.h>

#include <map>

namespace
{
	namespace Callbacks = AVPE::NativeInputCallbacks;
	using AVPE::NativeMenuInput::IntroSkipButtonVtable;
	using AVPE::NativeMenuInput::LoadErrorExitFunction;
	using AVPE::NativeMenuInput::LoadErrorMenuVtable;
	using AVPE::NativeMenuInput::MenuItemHotKeyActivate;

	class NativeInputCallbacksTest : public testing::Test
	{
	protected:
		static constexpr u32 callbacks = 0x00901000;
		static constexpr u32 error_menu = 0x00975000;

		std::map<u32, u32> words;
		std::map<u32, u32> handles;
		std::map<std::pair<u32, u32>, u32> members;
		u32 count = 0;
		Callbacks::Access read;

		void SetUp() override
		{
			read.word = [this](const u32 address, u32* value) { return Lookup(words, address, value); };
			read.is_object = [this](const u32 address) { return words.contains(address); };
			read.handle = [this](const u32 handle, u32* value) { return Lookup(handles, handle, value); };
			read.member = [this](const u32 owner, const u32 member, u32* value) {
				return Lookup(members, std::pair{owner, member}, value);
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

		u32 Register(const u32 owner, const u32 handle, const u32 vtable, const u32 function)
		{
			const u32 callback = callbacks + count * Callbacks::Stride;
			++count;
			words[owner] = vtable;
			handles[handle] = owner;
			words[callback + Callbacks::OwnerOffset] = handle;
			members[{owner, callback + Callbacks::MemberOffset}] = function;
			return callback;
		}

		bool Find(Callbacks::Target* target)
		{
			return Callbacks::FindRegistered(
				callbacks, count, LoadErrorMenuVtable, LoadErrorExitFunction, target, read);
		}
	};

	TEST_F(NativeInputCallbacksTest, FindsTheLoadErrorExitBoundToBothConfirmEvents)
	{
		// GLevelLoadErrorMenu registers FrontEndSelect, which the pad map binds to two events.
		const u32 first = Register(error_menu, 0x199A0001, LoadErrorMenuVtable, LoadErrorExitFunction);
		Register(error_menu, 0x199A0001, LoadErrorMenuVtable, LoadErrorExitFunction);

		Callbacks::Target target;
		ASSERT_TRUE(Find(&target));
		EXPECT_EQ(target.object, error_menu);
		EXPECT_EQ(target.callback, first);
		EXPECT_EQ(target.function, LoadErrorExitFunction);
	}

	TEST_F(NativeInputCallbacksTest, RejectsTwoOwnersOfTheClass)
	{
		Register(error_menu, 0x10000, LoadErrorMenuVtable, LoadErrorExitFunction);
		Register(error_menu + 0x1000, 0x20000, LoadErrorMenuVtable, LoadErrorExitFunction);

		Callbacks::Target target;
		EXPECT_FALSE(Find(&target));
	}

	TEST_F(NativeInputCallbacksTest, SkipsOwnersThatDoNotResolveOrDiffer)
	{
		words[callbacks + Callbacks::OwnerOffset] = 0x70000;
		++count;
		Register(0x01000000, 0x10000, 0x0035B3A0, LoadErrorExitFunction);
		Register(error_menu, 0x20000, LoadErrorMenuVtable, 0x00120F40);
		Callbacks::Target target;
		EXPECT_FALSE(Find(&target));

		const u32 exit = Register(error_menu, 0x20000, LoadErrorMenuVtable, LoadErrorExitFunction);
		ASSERT_TRUE(Find(&target));
		EXPECT_EQ(target.callback, exit);
	}

	TEST_F(NativeInputCallbacksTest, FindsTheIntroSkipHotKeyNotItsFocusKey)
	{
		// GSkipLevelIntro registers its hotkey and focus-key slots, each on two pad events.
		constexpr u32 skip_button = 0x016E6480;
		constexpr u32 focus_key_activate = 0x00120F90;
		Register(skip_button, 0x03110000, IntroSkipButtonVtable, focus_key_activate);
		const u32 hotkey = Register(skip_button, 0x03110000, IntroSkipButtonVtable, MenuItemHotKeyActivate);
		Register(skip_button, 0x03110000, IntroSkipButtonVtable, focus_key_activate);
		Register(skip_button, 0x03110000, IntroSkipButtonVtable, MenuItemHotKeyActivate);

		Callbacks::Target target;
		ASSERT_TRUE(Callbacks::FindRegistered(
			callbacks, count, IntroSkipButtonVtable, MenuItemHotKeyActivate, &target, read));
		EXPECT_EQ(target.object, skip_button);
		EXPECT_EQ(target.callback, hotkey);
	}
} // namespace
