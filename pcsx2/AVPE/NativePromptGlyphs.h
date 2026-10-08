// Identity of AVP:E's PS2 button-glyph meshes. Fork-local.

#pragma once

#include "common/Pcsx2Types.h"

#include <array>
#include <optional>

namespace AVPE
{
	enum class PromptButton : u8
	{
		Triangle,
		Cross,
		Circle,
		Square,
		R1,
	};

	// Every menu button icon is one of five CRendPS2Mesh publics in MASTER.TBD, named
	// by GetCRC of their label and found through the TBD fixup symbol table.
	class NativePromptGlyphs final
	{
	public:
		struct Glyph
		{
			u32 name_crc;
			PromptButton button;
		};

		static inline constexpr std::array<Glyph, 5> Glyphs{{
			{0x5F8391BF, PromptButton::Triangle}, // TopButton
			{0xA67152A0, PromptButton::Cross}, // BottomButton
			{0x361BAEBF, PromptButton::Circle}, // RightButton
			{0x0CCCE1E0, PromptButton::Square}, // LeftButton
			{0x0FA27263, PromptButton::R1}, // R1Button
		}};

		// pSymbolTable__16CTbdFixupManager -> {mask, buckets}; a bucket is
		// {entries, last index, capacity} and an entry {value, key, label}.
		static inline constexpr u32 SymbolTablePointer = 0x00367350;
		static inline constexpr u32 BucketSize = 0x0C;
		static inline constexpr u32 EntrySize = 0x0C;
		static inline constexpr u32 MaxBucketEntries = 4096;
		static inline constexpr u32 CRendPS2MeshVtable = 0x00334980;

		using GuestReader = bool (*)(u32 address, void* destination, u32 size);

		// The symbol value for `key`, as CHashTableElement::Find (0x001731A0) resolves it.
		static std::optional<u32> FindSymbol(u32 key, GuestReader read);

		// Re-resolves the glyph meshes; a glyph whose symbol is missing or is not a
		// CRendPS2Mesh resolves to nothing.
		void Resolve(GuestReader read);
		std::optional<PromptButton> ButtonFor(u32 resource) const;

	private:
		std::array<u32, Glyphs.size()> m_resources{};
	};
} // namespace AVPE
