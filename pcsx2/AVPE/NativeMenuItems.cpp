// AVP:E menu-item discovery. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMenuItems.h"
#include "AVPE/NativeAttractInput.h"

#include <array>

namespace AVPE::NativeMenuItems
{
	static constexpr u32 MISSION_GOALS_EXIT_VTABLE = 0x00342370;
	static constexpr u32 MENU_ITEM_HOTKEY_ACTIVATE = 0x00120F40;
	static constexpr u32 ACTIVATE_FOCUSED_ACTION = 0x21383159;
	static constexpr u32 MENU_ITEM_ACTION_OFFSET = 0x110;
	static constexpr u32 OBJECT_HANDLE_OFFSET = 0x18;
	static constexpr u32 FIRST_CHILD_OFFSET = 0x08;
	static constexpr u32 NEXT_SIBLING_OFFSET = 0x10;
	static constexpr u32 MAX_MENU_OBJECTS = 256;
	static constexpr u32 SLIDER_CONTROL_VTABLE = 0x00341E20;
	static constexpr u32 SLIDER_INPUT_DOWN = 0x001FD400;
	static constexpr u32 SLIDER_INPUT_UP = 0x001FD420;
	static constexpr u32 AUDIO_OPTIONS_VTABLE = 0x00341D20;
	static constexpr u32 AUDIO_BACK_BUTTON_ID = 0x0797F09F;
	static constexpr u32 OBJECT_NAME_OFFSET = 0x1C;

	static Status ReadMenuDescendants(const u32 menu,
		std::array<u32, MAX_MENU_OBJECTS>* descendants, u32* descendant_count, const char** error, const NativeInputCallbacks::Access& read)
	{
		*descendant_count = 0;
		std::array<u32, MAX_MENU_OBJECTS> pending{};
		u32 pending_count = 0;
		u32 first_child = 0;
		if (!read.word(menu + FIRST_CHILD_OFFSET, &first_child))
		{
			*error = "menu child list is unreadable";
			return Status::GuestMemoryError;
		}
		if (first_child != 0)
			pending[pending_count++] = first_child;

		while (pending_count != 0)
		{
			const u32 object = pending[--pending_count];
			bool already_visited = false;
			for (u32 index = 0; index < *descendant_count; ++index)
				already_visited = already_visited || (*descendants)[index] == object;
			if (already_visited)
				continue;
			if (*descendant_count >= descendants->size() || !read.is_object(object))
			{
				*error = "menu object tree is invalid or exceeds its bound";
				return Status::GuestMemoryError;
			}
			(*descendants)[(*descendant_count)++] = object;

			u32 child = 0;
			u32 sibling = 0;
			if (!read.word(object + FIRST_CHILD_OFFSET, &child) ||
				!read.word(object + NEXT_SIBLING_OFFSET, &sibling))
			{
				*error = "menu object tree is unreadable";
				return Status::GuestMemoryError;
			}
			const u32 additions = (child != 0 ? 1 : 0) + (sibling != 0 ? 1 : 0);
			if (pending_count + additions > pending.size())
			{
				*error = "menu object traversal exceeds its bound";
				return Status::GuestMemoryError;
			}
			if (sibling != 0)
				pending[pending_count++] = sibling;
			if (child != 0)
				pending[pending_count++] = child;
		}
		return Status::Success;
	}

	static bool ContainsValue(
		const std::array<u32, MAX_MENU_OBJECTS>& objects, const u32 count, const u32 object)
	{
		for (u32 index = 0; index < count; ++index)
		{
			if (objects[index] == object)
				return true;
		}
		return false;
	}

	static bool ReadDescendantHandles(const std::array<u32, MAX_MENU_OBJECTS>& descendants,
		const u32 descendant_count, std::array<u32, MAX_MENU_OBJECTS>* handles, const NativeInputCallbacks::Access& read)
	{
		for (u32 index = 0; index < descendant_count; ++index)
		{
			u32 resolved = 0;
			if (!read.word(descendants[index] + OBJECT_HANDLE_OFFSET, &(*handles)[index]) ||
				(*handles)[index] == 0 || !read.handle((*handles)[index], &resolved) ||
				resolved != descendants[index])
			{
				return false;
			}
		}
		return true;
	}

	static Status FindHotkeyCallback(const u32 entries, const u32 count, const u32 menu, const u32 focused,
		const u32 required_name,
		NativeInputCallbacks::Target* target, const char** error, const NativeInputCallbacks::Access& read)
	{
		*target = {};
		if (count > NativeInputCallbacks::MaxCount)
		{
			*error = "menu callback registry exceeds its bound";
			return Status::GuestMemoryError;
		}
		const NativeAttractInput::Result attract = NativeAttractInput::FindCancellation(entries, count, read);
		if (attract.status != NativeAttractInput::Status::Absent)
		{
			*error = attract.status == NativeAttractInput::Status::Available ?
			             "title attract input owner must finish cancellation before menu activation" :
			             attract.error;
			return attract.status == NativeAttractInput::Status::Invalid ? Status::GuestMemoryError : Status::AmbiguousMenu;
		}
		std::array<u32, MAX_MENU_OBJECTS> descendants{};
		u32 descendant_count = 0;
		const Status tree_status =
			ReadMenuDescendants(menu, &descendants, &descendant_count, error, read);
		if (tree_status != Status::Success)
			return tree_status;
		std::array<u32, MAX_MENU_OBJECTS> descendant_handles{};
		if (!ReadDescendantHandles(descendants, descendant_count, &descendant_handles, read))
		{
			*error = "menu descendant handle is invalid or unreadable";
			return Status::GuestMemoryError;
		}

		NativeInputCallbacks::Target focused_target;
		for (u32 index = 0; index < count; ++index)
		{
			const u32 callback = entries + index * NativeInputCallbacks::Stride;
			u32 owner_handle = 0;
			u32 owner = 0;
			if (!read.word(callback + NativeInputCallbacks::OwnerOffset, &owner_handle))
			{
				*error = "menu hotkey callback owner handle is unreadable";
				return Status::GuestMemoryError;
			}
			if (!ContainsValue(descendant_handles, descendant_count, owner_handle))
				continue;
			if (!read.handle(owner_handle, &owner) ||
				!ContainsValue(descendants, descendant_count, owner))
			{
				*error = "menu hotkey callback owner does not resolve to its descendant";
				return Status::GuestMemoryError;
			}

			u32 item_action = 0;
			if (!read.word(owner + MENU_ITEM_ACTION_OFFSET, &item_action))
			{
				*error = "menu hotkey item action is unreadable";
				return Status::GuestMemoryError;
			}
			if (required_name != 0)
			{
				u32 name = 0;
				if (!read.word(owner + OBJECT_NAME_OFFSET, &name))
				{
					*error = "menu hotkey item name is unreadable";
					return Status::GuestMemoryError;
				}
				if (name != required_name)
					continue;
			}
			else if (item_action != ACTIVATE_FOCUSED_ACTION && owner != focused)
				continue;

			u32 function = 0;
			if (!read.member(owner, callback + NativeInputCallbacks::MemberOffset, &function))
			{
				*error = "activation callback member is invalid or unreadable";
				return Status::GuestMemoryError;
			}
			if (function != MENU_ITEM_HOTKEY_ACTIVATE)
				continue;
			if (required_name == 0 && item_action != ACTIVATE_FOCUSED_ACTION)
			{
				if (focused_target.object == 0)
					focused_target = {.object = owner, .callback = callback, .function = function};
				continue;
			}
			if (target->object != 0 && target->object != owner)
			{
				*error = "more than one matching menu hotkey item is active";
				return Status::AmbiguousMenu;
			}
			if (target->object == 0)
				*target = {.object = owner, .callback = callback, .function = function};
		}

		if (target->object == 0)
			*target = focused_target;
		if (target->object == 0)
		{
			*error = "active menu has no registered activation hotkey or focused-item callback";
			return Status::FocusUnavailable;
		}
		return Status::Success;
	}

	Status FindActivationCallback(const u32 entries, const u32 count, const u32 menu, const u32 focused,
		NativeInputCallbacks::Target* target, const char** error, const NativeInputCallbacks::Access& read)
	{
		return FindHotkeyCallback(entries, count, menu, focused, 0, target, error, read);
	}

	Status FindCancellationCallback(const u32 entries, const u32 count, const u32 menu,
		NativeInputCallbacks::Target* target, const char** error, const NativeInputCallbacks::Access& read)
	{
		*target = {};
		u32 vtable = 0;
		if (!read.word(menu, &vtable))
		{
			*error = "cancel menu identity is unreadable";
			return Status::GuestMemoryError;
		}
		if (vtable != AUDIO_OPTIONS_VTABLE)
			return Status::FocusUnavailable;
		// GAudioOptionsMenu::ItemActivated (001FD640) restores preview audio
		// only for AudioBackButton. Generic GMenu::Cancel skips that lifecycle.
		const Status status = FindHotkeyCallback(entries, count, menu, 0, AUDIO_BACK_BUTTON_ID, target, error, read);
		if (status == Status::FocusUnavailable)
		{
			*error = "Audio options has no registered Back action";
			return Status::GuestMemoryError;
		}
		return status;
	}

	Status FindAdjustmentCallback(const u32 entries, const u32 count, const u32 menu,
		const u32 focused, const NativeMenuInput::Action action, NativeInputCallbacks::Target* target,
		const char** error, const NativeInputCallbacks::Access& read)
	{
		*target = {};
		using Action = NativeMenuInput::Action;
		if (action != Action::Left && action != Action::Right)
			return Status::FocusUnavailable;
		if (count > NativeInputCallbacks::MaxCount)
		{
			*error = "menu callback registry exceeds its bound";
			return Status::GuestMemoryError;
		}
		if (focused == 0)
			return Status::FocusUnavailable;
		u32 vtable = 0;
		if (!read.is_object(focused) || !read.word(focused, &vtable))
		{
			*error = "focused menu item is invalid or unreadable";
			return Status::GuestMemoryError;
		}
		if (vtable != SLIDER_CONTROL_VTABLE)
			return Status::FocusUnavailable;
		std::array<u32, MAX_MENU_OBJECTS> descendants{};
		u32 descendant_count = 0;
		const Status tree_status = ReadMenuDescendants(menu, &descendants, &descendant_count, error, read);
		if (tree_status != Status::Success)
			return tree_status;
		u32 focused_handle = 0;
		u32 resolved = 0;
		if (!ContainsValue(descendants, descendant_count, focused) ||
			!read.word(focused + OBJECT_HANDLE_OFFSET, &focused_handle) || focused_handle == 0 ||
			!read.handle(focused_handle, &resolved) || resolved != focused)
		{
			*error = "focused slider is not a valid menu descendant";
			return Status::GuestMemoryError;
		}
		// GSliderControl::Focus (001FD2C0) registers these original members;
		// its InputDown/InputUp own rate, clamping, and presentation callbacks.
		const u32 expected = action == Action::Left ? SLIDER_INPUT_DOWN : SLIDER_INPUT_UP;
		for (u32 index = 0; index < count; ++index)
		{
			const u32 callback = entries + index * NativeInputCallbacks::Stride;
			u32 owner_handle = 0;
			if (!read.word(callback + NativeInputCallbacks::OwnerOffset, &owner_handle))
			{
				*error = "slider callback owner handle is unreadable";
				return Status::GuestMemoryError;
			}
			if (owner_handle != focused_handle)
				continue;
			u32 function = 0;
			if (!read.member(focused, callback + NativeInputCallbacks::MemberOffset, &function))
			{
				*error = "slider callback member is invalid or unreadable";
				return Status::GuestMemoryError;
			}
			if (function == expected)
			{
				*target = {.object = focused, .callback = callback, .function = function};
				return Status::Success;
			}
		}
		*error = "focused slider has no registered adjustment callback";
		return Status::GuestMemoryError;
	}

	Status FindMissionGoalsExitItem(const u32 menu, u32* exit_item, const char** error, const NativeInputCallbacks::Access& read)
	{
		*exit_item = 0;
		std::array<u32, MAX_MENU_OBJECTS> descendants{};
		u32 descendant_count = 0;
		const Status tree_status =
			ReadMenuDescendants(menu, &descendants, &descendant_count, error, read);
		if (tree_status != Status::Success)
			return tree_status;
		for (u32 index = 0; index < descendant_count; ++index)
		{
			const u32 object = descendants[index];
			u32 vtable = 0;
			if (!read.word(object, &vtable))
			{
				*error = "mission-goals object vtable is unreadable";
				return Status::GuestMemoryError;
			}
			if (vtable == MISSION_GOALS_EXIT_VTABLE)
			{
				if (*exit_item != 0 && *exit_item != object)
				{
					*error = "more than one mission-goals exit item is active";
					return Status::AmbiguousMenu;
				}
				*exit_item = object;
			}
		}

		if (*exit_item == 0)
		{
			*error = "mission-goals exit item is not available";
			return Status::FocusUnavailable;
		}
		return Status::Success;
	}

} // namespace AVPE::NativeMenuItems
