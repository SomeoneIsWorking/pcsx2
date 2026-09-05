// AVP:E menu-item discovery. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMenuItems.h"

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
	static constexpr u32 CALLBACK_STRIDE = 0x18;
	static constexpr u32 CALLBACK_OWNER_OFFSET = 0x08;
	static constexpr u32 CALLBACK_MEMBER_OFFSET = 0x0C;
	static constexpr u32 CALLBACK_DIRECT_FUNCTION_OFFSET = 0x14;
	static constexpr u32 ATTRACT_EXIT_INPUT = 0x00206A60;
	static constexpr u32 ATTRACT_EXIT_ANALOG = 0x002069E0;
	static constexpr u32 MAX_MENU_OBJECTS = 256;

	static Status ReadMenuDescendants(const u32 menu,
		std::array<u32, MAX_MENU_OBJECTS>* descendants, u32* descendant_count, const char** error, const Access& read)
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
		const u32 descendant_count, std::array<u32, MAX_MENU_OBJECTS>* handles, const Access& read)
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

	Status FindActivationCallback(const u32 entries, const u32 count, const u32 menu, const u32 focused,
		CallbackTarget* target, const char** error, const Access& read)
	{
		*target = {};
		if (count > MAX_MENU_OBJECTS)
		{
			*error = "menu callback registry exceeds its bound";
			return Status::GuestMemoryError;
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

		CallbackTarget focused_target;
		for (u32 index = 0; index < count; ++index)
		{
			const u32 callback = entries + index * CALLBACK_STRIDE;
			u32 owner_handle = 0;
			u32 owner = 0;
			if (!read.word(callback + CALLBACK_OWNER_OFFSET, &owner_handle))
			{
				*error = "menu hotkey callback owner handle is unreadable";
				return Status::GuestMemoryError;
			}
			if (!ContainsValue(descendant_handles, descendant_count, owner_handle))
			{
				u32 direct_function = 0;
				if (!read.word(callback + CALLBACK_DIRECT_FUNCTION_OFFSET, &direct_function))
				{
					*error = "menu callback registry member is unreadable";
					return Status::GuestMemoryError;
				}
				if (direct_function == ATTRACT_EXIT_INPUT || direct_function == ATTRACT_EXIT_ANALOG)
				{
					u32 owner_vtable = 0;
					if (!read.handle(owner_handle, &owner) || !read.word(owner, &owner_vtable) ||
						owner_vtable != GuestObjects::AttractExitVtable)
					{
						*error = "title attract callback owner is invalid or unreadable";
						return Status::GuestMemoryError;
					}
					*error = "title attract input owner must finish cancellation before menu activation";
					return Status::AmbiguousMenu;
				}
				continue;
			}
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
			if (item_action != ACTIVATE_FOCUSED_ACTION && owner != focused)
				continue;

			u32 function = 0;
			if (!read.member(owner, callback + CALLBACK_MEMBER_OFFSET, &function))
			{
				*error = "activation callback member is invalid or unreadable";
				return Status::GuestMemoryError;
			}
			if (function != MENU_ITEM_HOTKEY_ACTIVATE)
				continue;
			if (item_action != ACTIVATE_FOCUSED_ACTION)
			{
				if (focused_target.object == 0)
					focused_target = {.object = owner, .callback = callback, .function = function};
				continue;
			}
			if (target->object != 0 && target->object != owner)
			{
				*error = "more than one ActivateFocused hotkey item is active";
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

	Status FindMissionGoalsExitItem(const u32 menu, u32* exit_item, const char** error, const Access& read)
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
