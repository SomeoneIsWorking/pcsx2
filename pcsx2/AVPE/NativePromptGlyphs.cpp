// Identity of AVP:E's PS2 button-glyph meshes. Fork-local.

#include "AVPE/NativePromptGlyphs.h"

#include "AVPE/GuestObjects.h"

#include <cstring>

namespace AVPE
{
	namespace
	{
		bool ReadU32(const NativePromptGlyphs::GuestReader read, const u32 address, u32* const value)
		{
			std::array<u8, 4> bytes{};
			if (!read(address, bytes.data(), static_cast<u32>(bytes.size())))
			{
				return false;
			}
			std::memcpy(value, bytes.data(), sizeof(*value));
			return true;
		}
	} // namespace

	std::optional<u32> NativePromptGlyphs::FindSymbol(const u32 key, const GuestReader read)
	{
		u32 table = 0;
		u32 mask = 0;
		u32 buckets = 0;
		if (!ReadU32(read, SymbolTablePointer, &table) || !GuestObjects::IsPlausibleAddress(table) ||
			!ReadU32(read, table, &mask) || !ReadU32(read, table + 4, &buckets) ||
			!GuestObjects::IsPlausibleAddress(buckets))
		{
			return std::nullopt;
		}
		const u32 bucket = buckets + (key & mask) * BucketSize;
		u32 entries = 0;
		u32 last_index = 0;
		if (!ReadU32(read, bucket, &entries) || !ReadU32(read, bucket + 4, &last_index))
		{
			return std::nullopt;
		}
		// Find scans indices 0..last_index inclusive; an empty bucket holds -1.
		const s32 last = static_cast<s32>(last_index);
		if (last < 0 || static_cast<u32>(last) >= MaxBucketEntries || !GuestObjects::IsPlausibleAddress(entries))
		{
			return std::nullopt;
		}
		for (u32 index = 0; index <= static_cast<u32>(last); index++)
		{
			const u32 entry = entries + index * EntrySize;
			u32 entry_key = 0;
			if (!ReadU32(read, entry + 4, &entry_key))
			{
				return std::nullopt;
			}
			if (entry_key == key)
			{
				u32 value = 0;
				if (!ReadU32(read, entry, &value))
				{
					return std::nullopt;
				}
				return value;
			}
		}
		return std::nullopt;
	}

	void NativePromptGlyphs::Resolve(const GuestReader read)
	{
		for (size_t index = 0; index < Glyphs.size(); index++)
		{
			m_resources[index] = 0;
			const std::optional<u32> mesh = FindSymbol(Glyphs[index].name_crc, read);
			u32 vtable = 0;
			if (mesh && GuestObjects::IsPlausibleAddress(*mesh) && ReadU32(read, *mesh, &vtable) &&
				vtable == CRendPS2MeshVtable)
			{
				m_resources[index] = *mesh;
			}
		}
	}

	std::optional<PromptButton> NativePromptGlyphs::ButtonFor(const u32 resource) const
	{
		if (resource == 0)
		{
			return std::nullopt;
		}
		for (size_t index = 0; index < Glyphs.size(); index++)
		{
			if (m_resources[index] == resource)
			{
				return Glyphs[index].button;
			}
		}
		return std::nullopt;
	}

	std::optional<NativeMenuInput::Action> NativePromptGlyphs::ActionFor(const PromptButton button)
	{
		switch (button)
		{
			case PromptButton::Cross:
				return NativeMenuInput::Action::Activate;
			case PromptButton::Triangle:
				return NativeMenuInput::Action::Cancel;
			case PromptButton::Circle:
			case PromptButton::Square:
			case PromptButton::R1:
				return std::nullopt;
		}
		return std::nullopt;
	}
} // namespace AVPE
