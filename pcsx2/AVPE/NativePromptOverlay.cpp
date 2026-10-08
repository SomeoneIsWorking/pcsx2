// PC-native replacement for AVP:E's PS2 button-prompt glyphs. Fork-local.

#include "AVPE/NativePromptOverlay.h"

#include "ImGui/ImGuiManager.h"

#include "imgui.h"

#include <algorithm>
#include <utility>

namespace AVPE
{
	namespace
	{
		NativePromptOverlay s_process_overlay;

		inline constexpr ImU32 KeyCapFill = IM_COL32(28, 30, 34, 255);
		inline constexpr ImU32 KeyCapBorder = IM_COL32(200, 204, 210, 255);
		inline constexpr ImU32 KeyCapText = IM_COL32(240, 242, 245, 255);
		inline constexpr float KeyCapRounding = 0.2f;
		inline constexpr float KeyCapBorderWidth = 0.08f;
		inline constexpr float LabelHeight = 0.55f;
		inline constexpr float LabelPadding = 0.3f;
	} // namespace

	void NativePromptOverlay::SetLabel(const NativeMenuInput::Action action, std::string label)
	{
		std::scoped_lock lock(m_label_mutex);
		m_labels[static_cast<size_t>(action)] = std::move(label);
	}

	void NativePromptOverlay::Publish(PromptFrame frame)
	{
		m_frame = std::move(frame);
	}

	void NativePromptOverlay::Clear()
	{
		m_frame = {};
	}

	DisplayRect NativePromptOverlay::TopOriginDisplay(
		const DisplayRect& presented, const bool lower_left_origin, const float window_height)
	{
		if (!lower_left_origin)
		{
			return presented;
		}
		const float height = presented.bottom - presented.top;
		const float top = window_height - presented.bottom;
		return {presented.left, top, presented.right, top + height};
	}

	DisplayRect NativePromptOverlay::MapToDisplay(
		const PromptRect& prompt, const PromptFrame& frame, const DisplayRect& display)
	{
		const float scale_x = (display.right - display.left) / frame.framebuffer_width;
		const float scale_y = (display.bottom - display.top) / frame.framebuffer_height;
		return {
			display.left + (prompt.left - CoverMargin) * scale_x,
			display.top + (prompt.top - CoverMargin) * scale_y,
			display.left + (prompt.right + CoverMargin) * scale_x,
			display.top + (prompt.bottom + CoverMargin) * scale_y,
		};
	}

	void NativePromptOverlay::Render(const DisplayRect& display) const
	{
		if (m_frame.prompts.empty() || m_frame.framebuffer_width <= 0.0f || m_frame.framebuffer_height <= 0.0f)
		{
			return;
		}
		ImDrawList* const draw = ImGui::GetBackgroundDrawList();
		ImFont* const font = ImGuiManager::GetStandardFont();
		std::scoped_lock lock(m_label_mutex);
		for (const PromptRect& prompt : m_frame.prompts)
		{
			const DisplayRect cover = MapToDisplay(prompt, m_frame, display);
			const float height = cover.bottom - cover.top;
			const std::string& label = m_labels[static_cast<size_t>(prompt.action)];
			const float text_size = height * LabelHeight;
			const ImVec2 text = font->CalcTextSizeA(text_size, FLT_MAX, 0.0f, label.c_str());
			const float centre_x = (cover.left + cover.right) * 0.5f;
			const float width = std::max(cover.right - cover.left, text.x + height * LabelPadding);
			const ImVec2 top_left(centre_x - width * 0.5f, cover.top);
			const ImVec2 bottom_right(centre_x + width * 0.5f, cover.bottom);
			draw->AddRectFilled(top_left, bottom_right, KeyCapFill, height * KeyCapRounding);
			draw->AddRect(top_left, bottom_right, KeyCapBorder, height * KeyCapRounding, height * KeyCapBorderWidth);
			draw->AddText(font, text_size, ImVec2(centre_x - text.x * 0.5f, cover.top + (height - text.y) * 0.5f),
				KeyCapText, label.c_str());
		}
	}

	NativePromptOverlay& NativePromptOverlay::Process()
	{
		return s_process_overlay;
	}
} // namespace AVPE
