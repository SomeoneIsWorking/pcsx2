// PC-native replacement for AVP:E's PS2 button-prompt glyphs. Fork-local.

#pragma once

#include "AVPE/NativeMenuInput.h"
#include "AVPE/NativePromptKeys.h"
#include "AVPE/PresentedDisplay.h"

#include <optional>
#include <string>
#include <vector>

namespace AVPE
{
	// One drawn prompt glyph, the menu item it belongs to and that item's PC key, in
	// guest framebuffer pixels (top-left origin).
	struct PromptRect
	{
		PromptKey key;
		u32 item = 0;
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	// Every prompt glyph one guest frame drew.
	struct PromptFrame
	{
		float framebuffer_width = 0.0f;
		float framebuffer_height = 0.0f;
		std::vector<PromptRect> prompts;

		// The item whose command key is `letter`.
		std::optional<u32> ItemForLetter(char letter) const;
	};

	// Covers each guest prompt glyph with an opaque key cap naming its PC key.
	// Frames arrive on the GS thread in guest order; the confirm and back labels come
	// from NativeKeyLabels.
	class NativePromptOverlay final
	{
	public:
		// The guest glyph disc has a dark rim one framebuffer pixel outside its quad.
		static inline constexpr float CoverMargin = 2.0f;

		void Publish(PromptFrame frame);
		void Clear();
		// Draws into the current ImGui frame; GS thread only.
		void Render(const DisplayRect& display) const;

		static DisplayRect MapToDisplay(const PromptRect& prompt, const PromptFrame& frame, const DisplayRect& display);
		static NativePromptOverlay& Process();

	private:
		static std::string LabelFor(const PromptKey& key);

		PromptFrame m_frame;
	};
} // namespace AVPE
