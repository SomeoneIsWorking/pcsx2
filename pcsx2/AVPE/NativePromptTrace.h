// Bounded observation of AVP:E's live font-render inputs. Fork-local.
#pragma once

#include "common/Pcsx2Defs.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

namespace AVPE
{
	class NativePromptTrace final
	{
	public:
		static inline constexpr u32 FontRenderPc = 0x001390E0;
		static inline constexpr size_t GlyphMapBytes = 256;
		static inline constexpr size_t TextBytes = 96;
		static inline constexpr size_t MaxFonts = 16;
		static inline constexpr size_t MaxTexts = 256;

		using GuestReader = bool (*)(u32 address, void* destination, u32 size);

		struct FontSample
		{
			u32 address = 0;
			std::array<u8, GlyphMapBytes> glyph_map{};
			u64 calls = 0;
		};

		struct TextSample
		{
			size_t font_index = 0;
			u32 render = 0;
			u32 resource = 0;
			std::array<u8, TextBytes> text{};
			size_t length = 0;
			bool truncated = false;
			u64 calls = 0;
		};

		struct Snapshot
		{
			bool armed = false;
			u64 observed_calls = 0;
			u64 invalid_reads = 0;
			u64 dropped_fonts = 0;
			u64 dropped_texts = 0;
			u64 truncated_text_calls = 0;
			std::vector<FontSample> fonts;
			std::vector<TextSample> texts;
		};

		NativePromptTrace();

		void Start();
		void Stop();
		void Reset();
		void Observe(u32 font, u32 render, GuestReader read);
		Snapshot Capture() const;

		static NativePromptTrace& Process();
		static bool ShouldInstrumentEePc(u32 pc);

	private:
		void ClearUnderLock();

		mutable std::mutex m_mutex;
		std::atomic_bool m_armed{false};
		u64 m_observed_calls = 0;
		u64 m_invalid_reads = 0;
		u64 m_dropped_fonts = 0;
		u64 m_dropped_texts = 0;
		u64 m_truncated_text_calls = 0;
		std::vector<FontSample> m_fonts;
		std::vector<TextSample> m_texts;
	};
} // namespace AVPE
