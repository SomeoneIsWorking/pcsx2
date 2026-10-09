// PC wording for AVP:E's PS2-specific TBD strings. Fork-local.

#include "AVPE/NativeTbdText.h"

#include "AVPE/NativeKeyLabels.h"

#include "vtlb.h"

#include <lucent/log.h>

#include <algorithm>
#include <array>
#include <vector>

namespace AVPE
{
	namespace
	{
		inline constexpr u32 EntrySize = 12;
		inline constexpr u32 MaxBucketEntries = 4096;

		bool ReadWord(const NativeTbdText::Access& access, const u32 address, u32* value)
		{
			std::array<u8, 4> bytes{};
			if (!access.read(address, bytes.data(), static_cast<u32>(bytes.size())))
			{
				return false;
			}
			*value = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
			         (static_cast<u32>(bytes[2]) << 16) | (static_cast<u32>(bytes[3]) << 24);
			return true;
		}

		enum class Lookup : u8
		{
			Found,
			Absent,
			Unreadable,
		};

		// CHashTableElement::Find over the symbol's bucket.
		Lookup FindSymbol(const NativeTbdText::Access& access, const u32 symbol, u32* value)
		{
			u32 table = 0;
			u32 mask = 0;
			u32 buckets = 0;
			if (!ReadWord(access, NativeTbdText::SymbolTablePointer, &table) || table == 0 ||
				!ReadWord(access, table, &mask) || !ReadWord(access, table + 4, &buckets))
			{
				return Lookup::Unreadable;
			}
			const u32 bucket = buckets + (symbol & mask) * EntrySize;
			u32 entries = 0;
			u32 last = 0;
			u32 capacity = 0;
			if (!ReadWord(access, bucket, &entries) || !ReadWord(access, bucket + 4, &last) ||
				!ReadWord(access, bucket + 8, &capacity) || capacity > MaxBucketEntries)
			{
				return Lookup::Unreadable;
			}
			const s32 last_index = static_cast<s32>(last);
			if (last_index >= static_cast<s32>(capacity))
			{
				return Lookup::Unreadable;
			}
			for (s32 index = 0; index <= last_index; ++index)
			{
				const u32 entry = entries + static_cast<u32>(index) * EntrySize;
				u32 crc = 0;
				if (!ReadWord(access, entry + 4, &crc))
				{
					return Lookup::Unreadable;
				}
				if (crc == symbol)
				{
					return ReadWord(access, entry, value) ? Lookup::Found : Lookup::Unreadable;
				}
			}
			return Lookup::Absent;
		}
	} // namespace

	NativeTbdText::Access NativeTbdText::Access::Guest()
	{
		return {
			.read = [](const u32 address, void* destination, const u32 size) { return vtlb_memSafeReadBytes(address, destination, size); },
			.write = [](const u32 address, const void* source, const u32 size) { return vtlb_memSafeWriteBytes(address, source, size); },
		};
	}

	NativeTbdText::Status NativeTbdText::RewriteConfirmPrompt(
		const ConfirmPrompt& prompt, const std::string_view confirm_label, const Access& access)
	{
		u32 text = 0;
		switch (FindSymbol(access, prompt.symbol, &text))
		{
			case Lookup::Found:
				break;
			case Lookup::Absent:
				return Status::Absent;
			case Lookup::Unreadable:
				return Status::Unreadable;
		}
		const size_t button = prompt.original.find(ConfirmButton);
		const std::string replacement = std::string(prompt.original.substr(0, button)) + std::string(confirm_label) +
		                                std::string(prompt.original.substr(button + ConfirmButton.size()));
		const u32 span = static_cast<u32>(prompt.original.size()) + 1;
		if (confirm_label.empty() || replacement.size() >= span)
		{
			return Status::Foreign;
		}
		std::vector<char> current(span);
		if (!access.read(text, current.data(), span))
		{
			return Status::Unreadable;
		}
		const std::string_view held(current.data(), span - 1);
		if (held.substr(0, replacement.size() + 1) == std::string_view(replacement.c_str(), replacement.size() + 1))
		{
			return Status::AlreadyNative;
		}
		if (held != prompt.original || current.back() != '\0')
		{
			return Status::Foreign;
		}
		std::vector<char> rewritten(span, '\0');
		std::copy(replacement.begin(), replacement.end(), rewritten.begin());
		if (!access.write(text, rewritten.data(), span))
		{
			return Status::Unreadable;
		}
		lucent::info("avpe-text", "prompt {:08x} at {:08x} now names {}", prompt.symbol, text, confirm_label);
		return Status::Rewritten;
	}

	void NativeTbdText::ObserveSetupPublicsExit()
	{
		const std::string confirm = NativeKeyLabels::Process().Get(NativeMenuInput::Action::Activate);
		for (const ConfirmPrompt& prompt : ConfirmPrompts)
		{
			RewriteConfirmPrompt(prompt, confirm, Access::Guest());
		}
	}
} // namespace AVPE
