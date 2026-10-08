// The in-game order card as a PC command card. Fork-local.

#include "AVPE/NativeCommandCard.h"

#include "AVPE/NativeInputDispatch.h"
#include "AVPE/NativeMenuItems.h"

#include <lucent/log.h>

namespace AVPE
{
	namespace
	{
		NativeCommandCard s_process_card;

		enum class Lookup : u8
		{
			Found,
			Absent,
			Invalid,
		};

		Lookup FindToggle(const u32 entries, const u32 count, const u32 function,
			NativeInputCallbacks::Target* target, const NativeInputCallbacks::Access& read)
		{
			*target = {};
			for (u32 index = 0; index < count; ++index)
			{
				const u32 callback = entries + index * NativeInputCallbacks::Stride;
				u32 handle = 0;
				u32 owner = 0;
				u32 vtable = 0;
				if (!read.word(callback + NativeInputCallbacks::OwnerOffset, &handle))
					return Lookup::Invalid;
				// Owners of every class register here; only a resolvable toggle is wanted.
				if (handle == 0 || !read.handle(handle, &owner) || !read.word(owner, &vtable) ||
					vtable != NativeCommandCard::ToggleMenuButtonVtable)
					continue;
				u32 resolved = 0;
				if (!read.member(owner, callback + NativeInputCallbacks::MemberOffset, &resolved))
					return Lookup::Invalid;
				if (resolved != function)
					continue;
				if (target->object != 0)
					return Lookup::Invalid;
				*target = {.object = owner, .callback = callback, .function = resolved};
			}
			return target->object != 0 ? Lookup::Found : Lookup::Absent;
		}

		bool ReadSelectionCount(const NativeInputCallbacks::Access& read, u32* count)
		{
			u32 pointer = 0;
			u32 selection = 0;
			return read.word(NativeCommandCard::PointerInstance, &pointer) && read.is_object(pointer) &&
			       read.word(pointer + NativeCommandCard::SelectionOffset, &selection) && selection != 0 &&
			       read.word(selection + 4, count);
		}
	} // namespace

	bool NativeCommandCard::Command(const char letter)
	{
		std::lock_guard lock(m_mutex);
		if (m_stage != Stage::Idle || letter < 'A' || letter > 'Z')
			return false;
		m_stage = Stage::Open;
		m_letter = letter;
		m_opened = false;
		m_hide = false;
		return true;
	}

	bool NativeCommandCard::Show(const bool shown)
	{
		std::lock_guard lock(m_mutex);
		if (shown)
		{
			if (m_stage != Stage::Idle)
				return false;
			m_stage = Stage::Open;
			m_letter = 0;
			m_opened = false;
			m_hide = false;
			return true;
		}
		if (m_stage == Stage::Idle || (m_stage == Stage::Open && m_letter == 0))
			m_stage = Stage::Close;
		else
			m_hide = true;
		return true;
	}

	void NativeCommandCard::Step(const u32 input_device, const Guest& guest)
	{
		std::lock_guard lock(m_mutex);
		if (m_stage == Stage::Idle || !guest.dispatch_idle())
			return;
		m_stage = Advance(input_device, guest);
	}

	NativeCommandCard::Stage NativeCommandCard::Advance(const u32 input_device, const Guest& guest)
	{
		const NativeInputCallbacks::Access& read = guest.read;
		u32 in_game_menu = 0;
		if (!read.word(InGameMenuPointer, &in_game_menu) || !read.is_object(in_game_menu))
			return Stage::Idle;
		u32 flags = 0;
		u32 entries = 0;
		u32 count = 0;
		if (!read.word(in_game_menu + (CardShownOffset & ~3u), &flags) ||
			!read.word(input_device + CallbackArrayOffset, &entries) ||
			!read.word(input_device + CallbackArrayOffset + 4, &count) || count > NativeInputCallbacks::MaxCount)
		{
			lucent::warn("avpe-command-card", "in-game menu or callback registry is unreadable");
			return Stage::Idle;
		}
		const bool shown = ((flags >> ((CardShownOffset & 3u) * 8)) & 0xFF) != 0;

		NativeInputCallbacks::Target toggle;
		switch (m_stage)
		{
			case Stage::Open:
			{
				if (shown)
					return m_letter != 0 ? Fire(entries, count, in_game_menu, guest) : Stage::Idle;
				u32 selected = 0;
				if (m_letter != 0 && (!ReadSelectionCount(read, &selected) || selected == 0))
					return Stage::Idle;
				if (FindToggle(entries, count, ToggleOpenFunction, &toggle, read) != Lookup::Found ||
					!guest.queue(toggle))
				{
					lucent::warn("avpe-command-card", "the order card toggle is not registered once");
					return Stage::Idle;
				}
				m_opened = true;
				return m_letter != 0 ? Stage::Fire : Stage::Idle;
			}
			case Stage::Fire:
				if (!shown)
				{
					lucent::info("avpe-command-card", "the order card did not open for key {}", m_letter);
					return Stage::Idle;
				}
				return Fire(entries, count, in_game_menu, guest);
			case Stage::Close:
				if (!shown)
					return Stage::Idle;
				if (FindToggle(entries, count, ToggleCloseFunction, &toggle, read) != Lookup::Found ||
					!guest.queue(toggle))
				{
					lucent::warn("avpe-command-card", "the order card toggle is not registered once");
				}
				return Stage::Idle;
			case Stage::Idle:
				break;
		}
		return Stage::Idle;
	}

	NativeCommandCard::Stage NativeCommandCard::Fire(
		const u32 entries, const u32 count, const u32 in_game_menu, const Guest& guest)
	{
		u32 card = 0;
		NativeInputCallbacks::Target item;
		const char* error = "";
		if (!guest.read.word(in_game_menu + CurrentMenuOffset, &card) ||
			NativeMenuItems::FindCommandItem(entries, count, card, m_letter, &item, &error, guest.read) !=
				NativeMenuItems::Status::Success)
		{
			lucent::info("avpe-command-card", "no order for key {}: {}", m_letter, error);
			return Finish();
		}
		if (!guest.queue(item))
			lucent::warn("avpe-command-card", "order {:08x} for key {} was not queued", item.object, m_letter);
		return Finish();
	}

	NativeCommandCard::Stage NativeCommandCard::Finish() const
	{
		return m_opened || m_hide ? Stage::Close : Stage::Idle;
	}

	void NativeCommandCard::Reset()
	{
		std::lock_guard lock(m_mutex);
		m_stage = Stage::Idle;
		m_letter = 0;
		m_opened = false;
		m_hide = false;
	}

	NativeCommandCard& NativeCommandCard::Process()
	{
		return s_process_card;
	}

	NativeCommandCard::Guest NativeCommandCard::LiveGuest()
	{
		return {
			.read = {},
			.dispatch_idle = NativeInputDispatch::IsIdle,
			.queue =
				[](const NativeInputCallbacks::Target& target) {
					return NativeInputDispatch::QueueMenuAction(
						{.target = target.object, .callback = target.callback, .function = target.function})
			            .Succeeded();
				},
		};
	}
} // namespace AVPE
