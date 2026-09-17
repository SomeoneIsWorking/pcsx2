// Bounded observation of AVP:E's live font-render inputs. Fork-local.

#include "AVPE/NativePromptTrace.h"

#include "AVPE/GuestObjects.h"

#include <algorithm>

namespace AVPE
{
	namespace
	{
		NativePromptTrace s_process_trace;
	} // namespace

	NativePromptTrace::NativePromptTrace()
	{
		m_fonts.reserve(MaxFonts);
		m_texts.reserve(MaxTexts);
	}

	void NativePromptTrace::Start()
	{
		std::scoped_lock lock(m_mutex);
		m_armed.store(false, std::memory_order_release);
		ClearUnderLock();
		m_armed.store(true, std::memory_order_release);
	}

	void NativePromptTrace::Stop()
	{
		std::scoped_lock lock(m_mutex);
		m_armed.store(false, std::memory_order_release);
	}

	void NativePromptTrace::Reset()
	{
		std::scoped_lock lock(m_mutex);
		m_armed.store(false, std::memory_order_release);
		ClearUnderLock();
	}

	void NativePromptTrace::ClearUnderLock()
	{
		m_observed_calls = 0;
		m_invalid_reads = 0;
		m_dropped_fonts = 0;
		m_dropped_texts = 0;
		m_truncated_text_calls = 0;
		m_fonts.clear();
		m_texts.clear();
	}

	bool NativePromptTrace::ShouldInstrumentEePc(const u32 pc)
	{
		return pc == FontRenderPc;
	}

	NativePromptTrace& NativePromptTrace::Process()
	{
		return s_process_trace;
	}

	void NativePromptTrace::Observe(const u32 font, const u32 render, const GuestReader read)
	{
		if (!m_armed.load(std::memory_order_acquire))
		{
			return;
		}
		std::scoped_lock lock(m_mutex);
		if (!m_armed.load(std::memory_order_acquire))
		{
			return;
		}
		++m_observed_calls;

		std::array<u8, 4> resource_bytes{};
		std::array<u8, GlyphMapBytes> glyph_map{};
		std::array<u8, TextBytes> text{};
		if (read == nullptr || !GuestObjects::IsPlausibleAddress(font) ||
			!GuestObjects::IsPlausibleAddress(render) ||
			!read(render + 0x20, resource_bytes.data(), resource_bytes.size()) ||
			!read(font + 0x24, glyph_map.data(), glyph_map.size()))
		{
			++m_invalid_reads;
			return;
		}
		const u32 resource = static_cast<u32>(resource_bytes[0]) |
		                     (static_cast<u32>(resource_bytes[1]) << 8) |
		                     (static_cast<u32>(resource_bytes[2]) << 16) |
		                     (static_cast<u32>(resource_bytes[3]) << 24);
		if (!GuestObjects::IsPlausibleAddress(resource) ||
			!read(resource + 0x0C, text.data(), text.size()))
		{
			++m_invalid_reads;
			return;
		}

		const auto terminator = std::find(text.begin(), text.end(), 0);
		const size_t length = static_cast<size_t>(terminator - text.begin());
		const bool truncated = terminator == text.end();
		if (truncated)
		{
			++m_truncated_text_calls;
		}

		auto font_sample = std::find_if(m_fonts.begin(), m_fonts.end(), [&](const FontSample& sample) {
			return sample.address == font && sample.glyph_map == glyph_map;
		});
		if (font_sample == m_fonts.end())
		{
			if (m_fonts.size() >= MaxFonts)
			{
				++m_dropped_fonts;
				return;
			}
			m_fonts.push_back(FontSample{.address = font, .glyph_map = glyph_map, .calls = 0});
			font_sample = m_fonts.end() - 1;
		}
		++font_sample->calls;
		const size_t font_index = static_cast<size_t>(font_sample - m_fonts.begin());
		auto text_sample = std::find_if(m_texts.begin(), m_texts.end(), [&](const TextSample& sample) {
			return sample.font_index == font_index && sample.render == render &&
			       sample.resource == resource && sample.length == length &&
			       std::equal(sample.text.begin(), sample.text.begin() + length, text.begin());
		});
		if (text_sample == m_texts.end())
		{
			if (m_texts.size() >= MaxTexts)
			{
				++m_dropped_texts;
				return;
			}
			m_texts.push_back(TextSample{.font_index = font_index, .render = render, .resource = resource, .text = text, .length = length, .truncated = truncated, .calls = 1});
			return;
		}
		++text_sample->calls;
	}

	NativePromptTrace::Snapshot NativePromptTrace::Capture() const
	{
		std::scoped_lock lock(m_mutex);
		return Snapshot{
			.armed = m_armed.load(std::memory_order_acquire),
			.observed_calls = m_observed_calls,
			.invalid_reads = m_invalid_reads,
			.dropped_fonts = m_dropped_fonts,
			.dropped_texts = m_dropped_texts,
			.truncated_text_calls = m_truncated_text_calls,
			.fonts = m_fonts,
			.texts = m_texts,
		};
	}
} // namespace AVPE
