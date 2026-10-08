// PC keys for AVP:E's prompted menu items. Fork-local.

#include "AVPE/NativePromptKeys.h"

namespace AVPE
{
	PromptKey::Kind NativePromptKeys::KindFor(const u32 hotkey, const PromptButton button)
	{
		if (hotkey == 0)
		{
			if (button == PromptButton::Cross)
			{
				return PromptKey::Kind::Confirm;
			}
			return button == PromptButton::Triangle ? PromptKey::Kind::Back : PromptKey::Kind::Command;
		}
		if (hotkey == FrontEndSelect)
		{
			return PromptKey::Kind::Confirm;
		}
		if (IsBackHotkey(hotkey))
		{
			return PromptKey::Kind::Back;
		}
		return PromptKey::Kind::Command;
	}

	bool NativePromptKeys::IsBackHotkey(const u32 hotkey)
	{
		return hotkey == FrontEndBack || hotkey == MenuTriangleRelease;
	}

	PromptKey NativePromptKeys::Next(const u32 hotkey, const PromptButton button, const std::string_view label)
	{
		const PromptKey::Kind kind = KindFor(hotkey, button);
		if (kind != PromptKey::Kind::Command)
		{
			return {kind, 0};
		}
		for (const char character : label)
		{
			const char upper = (character >= 'a' && character <= 'z') ? static_cast<char>(character - 'a' + 'A') : character;
			if (upper >= 'A' && upper <= 'Z' && !m_taken.test(static_cast<size_t>(upper - 'A')))
			{
				m_taken.set(static_cast<size_t>(upper - 'A'));
				return {kind, upper};
			}
		}
		for (size_t index = 0; index < m_taken.size(); index++)
		{
			if (!m_taken.test(index))
			{
				m_taken.set(index);
				return {kind, static_cast<char>('A' + index)};
			}
		}
		return {kind, 0};
	}
} // namespace AVPE
