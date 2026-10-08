// PC-native replacement for AVP:E's PS2 button-prompt glyphs. Fork-local.

#pragma once

#include "AVPE/NativeMenuInput.h"

#include <array>
#include <mutex>
#include <string>
#include <vector>

namespace AVPE
{
	// One drawn prompt glyph and the menu action its button triggers, in guest
	// framebuffer pixels (top-left origin).
	struct PromptRect
	{
		NativeMenuInput::Action action = NativeMenuInput::Action::Activate;
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
	};

	// Presented-window rectangle, top-left origin.
	struct DisplayRect
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	// Covers each guest prompt glyph with an opaque key cap naming the bound PC key.
	// Frames arrive on the GS thread in guest order; labels come from the host's
	// binding owner.
	class NativePromptOverlay final
	{
	public:
		// The guest glyph disc has a dark rim one framebuffer pixel outside its quad.
		static inline constexpr float CoverMargin = 2.0f;

		void SetLabel(NativeMenuInput::Action action, std::string label);
		void Publish(PromptFrame frame);
		void Clear();
		// Draws into the current ImGui frame; GS thread only.
		void Render(const DisplayRect& display) const;

		// The present rect is flipped on lower-left-origin devices; the overlay draws top-down.
		static DisplayRect TopOriginDisplay(const DisplayRect& presented, bool lower_left_origin, float window_height);
		static DisplayRect MapToDisplay(const PromptRect& prompt, const PromptFrame& frame, const DisplayRect& display);
		static NativePromptOverlay& Process();

	private:
		mutable std::mutex m_label_mutex;
		std::array<std::string, NativeMenuInput::ActionCount> m_labels;
		PromptFrame m_frame;
	};
} // namespace AVPE
