// PC keys for in-mission unit commands. Fork-local.

#include "AVPE/NativeUnitCommands.h"

#include "AVPE/NativeMenuItems.h"

#include <lucent/log.h>

namespace AVPE
{
	namespace
	{
		using NativeInputCallbacks::FindRegistered;

		NativeUnitCommands s_process_commands;

		bool ReadSelectionCount(const NativeInputCallbacks::Access& read, u32* count)
		{
			u32 pointer = 0;
			u32 selection = 0;
			return read.word(NativeUnitCommands::PointerInstance, &pointer) && read.is_object(pointer) &&
			       read.word(pointer + NativeUnitCommands::SelectionOffset, &selection) && selection != 0 &&
			       read.word(selection + 4, count);
		}
	} // namespace

	bool NativeUnitCommands::Begin(const std::initializer_list<Action> actions, const char letter, const u32 hotkey)
	{
		if (m_next < m_count)
			return false;
		m_count = 0;
		for (const Action action : actions)
			m_actions[m_count++] = action;
		m_next = 0;
		m_letter = letter;
		m_hotkey = hotkey;
		m_opened = false;
		m_hide = false;
		return true;
	}

	bool NativeUnitCommands::CardOrder(const char letter)
	{
		std::lock_guard lock(m_mutex);
		if (letter < 'A' || letter > 'Z')
			return false;
		return Begin({Action::OpenCard, Action::FireCardOrder, Action::CloseCard}, letter);
	}

	bool NativeUnitCommands::ShowCard(const bool shown)
	{
		std::lock_guard lock(m_mutex);
		if (shown)
			return Begin({Action::OpenCard}, 0);
		if (!Begin({Action::CloseCard}, 0) && m_actions[m_next] == Action::OpenCard && m_letter == 0)
			m_actions[m_next] = Action::CloseCard;
		m_hide = true;
		return true;
	}

	bool NativeUnitCommands::RecallGroup(const u32 group)
	{
		std::lock_guard lock(m_mutex);
		return group < GroupHotkeys.size() && Begin({Action::FireMenuItem}, 0, GroupHotkeys[group]);
	}

	bool NativeUnitCommands::AssignGroup(const u32 group)
	{
		std::lock_guard lock(m_mutex);
		return group < GroupHotkeys.size() &&
		       Begin({Action::PressGrouping, Action::FireMenuItem, Action::ReleaseGrouping}, 0, GroupHotkeys[group]);
	}

	bool NativeUnitCommands::JumpToEvent()
	{
		std::lock_guard lock(m_mutex);
		return Begin({Action::FireMenuItem}, 0, EventHotkey);
	}

	bool NativeUnitCommands::JumpToBase()
	{
		std::lock_guard lock(m_mutex);
		return Begin({Action::FireMenuItem}, 0, BaseHotkey);
	}

	bool NativeUnitCommands::UseSpecial()
	{
		std::lock_guard lock(m_mutex);
		return Begin({Action::FireMenuItem}, 0, SpecialHotkey);
	}

	void NativeUnitCommands::Step(const u32 input_device, const Guest& guest)
	{
		std::lock_guard lock(m_mutex);
		if (m_next >= m_count || !guest.dispatch_idle())
			return;

		const NativeInputCallbacks::Access& read = guest.read;
		Frame frame;
		u32 flags = 0;
		if (!read.word(InGameMenuPointer, &frame.in_game_menu) || !read.is_object(frame.in_game_menu))
		{
			m_next = m_count;
			return;
		}
		NativeInputCallbacks::Registry registry;
		if (!read.word(frame.in_game_menu + (CardShownOffset & ~3u), &flags) ||
			!NativeInputCallbacks::ReadRegistry(input_device, read, &registry))
		{
			lucent::warn("avpe-unit-commands", "in-game menu or callback registry is unreadable");
			m_next = m_count;
			return;
		}
		frame.entries = registry.entries;
		frame.count = registry.count;
		frame.card_shown = ((flags >> ((CardShownOffset & 3u) * 8)) & 0xFF) != 0;

		bool queued = false;
		while (m_next < m_count && !queued)
		{
			if (!Run(m_actions[m_next++], frame, guest, &queued))
				m_next = m_count;
		}
		if (m_next >= m_count && m_hide)
		{
			Begin({Action::CloseCard}, 0);
			m_hide = true;
		}
	}

	bool NativeUnitCommands::Run(const Action action, const Frame& frame, const Guest& guest, bool* queued)
	{
		NativeInputCallbacks::Target target;
		switch (action)
		{
			case Action::OpenCard:
			{
				if (frame.card_shown)
					return true;
				u32 selected = 0;
				if (m_letter != 0 && (!ReadSelectionCount(guest.read, &selected) || selected == 0))
					return false;
				if (!FindRegistered(frame.entries, frame.count, ToggleMenuButtonVtable, ToggleOpenFunction, &target,
						guest.read) ||
					!guest.queue(target))
				{
					lucent::warn("avpe-unit-commands", "the order card toggle is not registered once");
					return false;
				}
				m_opened = true;
				*queued = true;
				return true;
			}
			case Action::FireCardOrder:
			{
				if (!frame.card_shown)
				{
					lucent::info("avpe-unit-commands", "the order card did not open for key {}", m_letter);
					return false;
				}
				u32 card = 0;
				const char* error = "";
				if (!guest.read.word(frame.in_game_menu + CurrentMenuOffset, &card) ||
					NativeMenuItems::FindCommandItem(frame.entries, frame.count, card, m_letter, &target, &error,
						guest.read) != NativeMenuItems::Status::Success)
				{
					lucent::info("avpe-unit-commands", "no order for key {}: {}", m_letter, error);
					return true;
				}
				*queued = guest.queue(target);
				if (!*queued)
					lucent::warn("avpe-unit-commands", "order {:08x} for key {} was not queued", target.object, m_letter);
				return true;
			}
			case Action::CloseCard:
			{
				const bool close = m_opened || m_hide;
				m_opened = false;
				m_hide = false;
				if (!close || !frame.card_shown)
					return true;
				if (!FindRegistered(frame.entries, frame.count, ToggleMenuButtonVtable, ToggleCloseFunction, &target,
						guest.read) ||
					!guest.queue(target))
				{
					lucent::warn("avpe-unit-commands", "the order card toggle is not registered once");
					return false;
				}
				*queued = true;
				return true;
			}
			case Action::PressGrouping:
			case Action::ReleaseGrouping:
			{
				// The order card replaces the unit menu, whose group items share the d-pad events.
				if (action == Action::PressGrouping && frame.card_shown)
					return false;
				const u32 function =
					action == Action::PressGrouping ? GroupingPressFunction : GroupingReleaseFunction;
				if (!FindRegistered(frame.entries, frame.count, GroupingButtonVtable, function, &target, guest.read) ||
					!guest.queue(target))
				{
					lucent::warn("avpe-unit-commands", "the grouping button is not registered once");
					return false;
				}
				*queued = true;
				return true;
			}
			case Action::FireMenuItem:
			{
				if (frame.card_shown)
				{
					lucent::info("avpe-unit-commands", "event {:08x} ignored while the order card is shown", m_hotkey);
					return true;
				}
				u32 menu = 0;
				const char* error = "";
				if (!guest.read.word(frame.in_game_menu + CurrentMenuOffset, &menu) ||
					NativeMenuItems::FindHotkeyItem(frame.entries, frame.count, menu, m_hotkey, &target, &error,
						guest.read) != NativeMenuItems::Status::Success)
				{
					lucent::info("avpe-unit-commands", "no unit-menu item for event {:08x}: {}", m_hotkey, error);
					return true;
				}
				*queued = guest.queue(target);
				if (!*queued)
					lucent::warn("avpe-unit-commands", "item {:08x} for event {:08x} was not queued", target.object, m_hotkey);
				return true;
			}
		}
		return false;
	}

	void NativeUnitCommands::Reset()
	{
		std::lock_guard lock(m_mutex);
		m_count = 0;
		m_next = 0;
		m_letter = 0;
		m_hotkey = 0;
		m_opened = false;
		m_hide = false;
	}

	NativeUnitCommands& NativeUnitCommands::Process()
	{
		return s_process_commands;
	}
} // namespace AVPE
