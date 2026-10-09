#include "AVPE/NativeMouseButtons.h"

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <utility>
#include <vector>

namespace
{
	using AVPE::NativeMouseButtons;
	using AVPE::NativeInput::ButtonEdge;
	using AVPE::NativeInput::MouseButton;
	using AVPE::NativeInput::SelectionMode;
	namespace Callbacks = AVPE::NativeInputCallbacks;

	using Calls = std::vector<std::pair<u32, std::array<u64, 4>>>;

	// The registry as the game leaves it each frame while units are selected: the DWIM
	// buttons for what the cursor is over, with hotkey and focus slots bound to Circle.
	class NativeMouseButtonsTest : public testing::Test
	{
	protected:
		static constexpr u32 entries = 0x00A61000;
		static constexpr u32 pointer = 0x0152AB30;
		static constexpr u32 menu = 0x015B03E0;
		static constexpr u32 attack = 0x014DA5B0;
		static constexpr u32 move = 0x01509CB0;
		std::map<u32, u32> words;
		std::map<u32, u32> handles;
		std::map<std::pair<u32, u32>, u32> members;
		NativeMouseButtons buttons;
		NativeMouseButtons::Frame frame{.pointer = pointer, .in_game_menu = menu, .registry = {.entries = entries}};
		Callbacks::Access read;

		void SetUp() override
		{
			read.word = [this](const u32 address, u32* value) { return Lookup(words, address, value); };
			read.is_object = [this](const u32 address) { return words.contains(address); };
			read.is_address = [](const u32 address) { return address != 0; };
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

		void Register(const u32 owner, const u32 vtable, const u32 focus, const u32 hotkey)
		{
			const u32 handle = (owner & 0xFFFF0) << 4;
			words[owner] = vtable;
			handles[handle] = owner;
			for (const u32 function : {hotkey, focus})
			{
				const u32 callback = entries + frame.registry.count++ * Callbacks::Stride;
				words[callback + Callbacks::OwnerOffset] = handle;
				members[{owner, callback + Callbacks::MemberOffset}] = function;
			}
		}

		void RegisterAttack()
		{
			Register(attack, NativeMouseButtons::DwimButtonVtable, Callbacks::MenuItemFocusKeyActivate,
				Callbacks::MenuItemHotKeyActivate);
		}

		void RegisterMove()
		{
			Register(move, NativeMouseButtons::MoveDwimButtonVtable, NativeMouseButtons::MoveDwimFocusKeyActivate,
				NativeMouseButtons::MoveDwimHotKeyActivate);
		}

		Calls Edge(const MouseButton button, const ButtonEdge edge, const SelectionMode mode = SelectionMode::Replace)
		{
			Calls calls;
			for (const AVPE::EECallShuttle::Request& request : buttons.Calls(button, edge, mode, frame, read))
			{
				calls.emplace_back(request.function, request.arguments);
			}
			return calls;
		}

		Calls Click(const MouseButton button, const SelectionMode mode = SelectionMode::Replace)
		{
			Calls calls = Edge(button, ButtonEdge::Press);
			const Calls release = Edge(button, ButtonEdge::Release, mode);
			calls.insert(calls.end(), release.begin(), release.end());
			return calls;
		}
	};

	TEST_F(NativeMouseButtonsTest, AClickIsThePointersMouseHandlers)
	{
		const Calls expected{{NativeMouseButtons::PressMousePrimaryFunction, {pointer, 0, 0, 0}},
			{NativeMouseButtons::ReleaseMousePrimaryFunction, {pointer, 0, 0, 0}}};
		EXPECT_EQ(Click(MouseButton::Primary), expected);
	}

	TEST_F(NativeMouseButtonsTest, ShiftReleaseSelectsAdditivelyThenRefreshesTheMenu)
	{
		const Calls expected{{NativeMouseButtons::SelectChangingFunction, {pointer, 0, 1, 0}},
			{NativeMouseButtons::InGameMenuRefreshFunction, {menu, 0, 0, 0}}};
		EXPECT_EQ(Edge(MouseButton::Primary, ButtonEdge::Release, SelectionMode::Toggle), expected);
	}

	TEST_F(NativeMouseButtonsTest, CtrlReleaseSelectsThenWidensToTheType)
	{
		const Calls expected{{NativeMouseButtons::SelectChangingFunction, {pointer, 0, 0, 0}},
			{NativeMouseButtons::DoubleClickSelectChangingFunction, {pointer, 0, 0, 0}},
			{NativeMouseButtons::InGameMenuRefreshFunction, {menu, 0, 0, 0}}};
		EXPECT_EQ(Edge(MouseButton::Primary, ButtonEdge::Release, SelectionMode::SameType), expected);
	}

	TEST_F(NativeMouseButtonsTest, RightClickOnAnEnemyRunsTheAttackButton)
	{
		RegisterAttack();
		const Calls expected{{Callbacks::MenuItemFocusKeyActivate, {attack, 0, 0, 0}},
			{Callbacks::MenuItemHotKeyActivate, {attack, 0, 0, 0}}};
		EXPECT_EQ(Click(MouseButton::Secondary), expected);
	}

	TEST_F(NativeMouseButtonsTest, RightClickOnGroundRunsTheMoveButton)
	{
		RegisterMove();
		const Calls expected{{NativeMouseButtons::MoveDwimFocusKeyActivate, {move, 0, 0, 0}},
			{NativeMouseButtons::MoveDwimHotKeyActivate, {move, 0, 0, 0}}};
		EXPECT_EQ(Click(MouseButton::Secondary), expected);
	}

	TEST_F(NativeMouseButtonsTest, RightClickWithoutAContextButtonDoesNothing)
	{
		EXPECT_TRUE(Click(MouseButton::Secondary).empty());
	}

	TEST_F(NativeMouseButtonsTest, TheRightReleaseEndsTheButtonThePressFocused)
	{
		RegisterMove();
		ASSERT_EQ(Edge(MouseButton::Secondary, ButtonEdge::Press).size(), 1U);
		RegisterAttack();
		const Calls expected{{NativeMouseButtons::MoveDwimHotKeyActivate, {move, 0, 0, 0}}};
		EXPECT_EQ(Edge(MouseButton::Secondary, ButtonEdge::Release), expected);
	}

	TEST_F(NativeMouseButtonsTest, ARightReleaseWithoutAFocusedButtonDoesNothing)
	{
		RegisterAttack();
		EXPECT_TRUE(Edge(MouseButton::Secondary, ButtonEdge::Release).empty());
	}
} // namespace
