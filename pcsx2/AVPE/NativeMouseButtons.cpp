// The guest calls each mouse edge makes. Fork-local.

#include "AVPE/NativeMouseButtons.h"

namespace AVPE
{
	namespace
	{
		using NativeInput::ButtonEdge;
		using NativeInput::MouseButton;
		using NativeInput::SelectionMode;

		NativeMouseButtons s_process_mouse_buttons;

		EECallShuttle::Request Call(const u32 function, const u32 a0, const u32 a1 = 0, const u32 a2 = 0)
		{
			return {.function = function, .arguments = {a0, a1, a2, 0}};
		}
	} // namespace

	std::vector<EECallShuttle::Request> NativeMouseButtons::Calls(const MouseButton button, const ButtonEdge edge,
		const SelectionMode mode, const Frame& frame, const NativeInputCallbacks::Access& read)
	{
		if (button == MouseButton::Primary)
		{
			if (edge == ButtonEdge::Press)
			{
				return {Call(PressMousePrimaryFunction, frame.pointer)};
			}
			// Each ends as Input_ReleaseMouse1 does, with GInGameMenu::Refresh.
			switch (mode)
			{
				case SelectionMode::Toggle:
					return {Call(SelectChangingFunction, frame.pointer, 0, 1),
						Call(InGameMenuRefreshFunction, frame.in_game_menu)};
				case SelectionMode::SameType:
					return {Call(SelectChangingFunction, frame.pointer), Call(DoubleClickSelectChangingFunction, frame.pointer),
						Call(InGameMenuRefreshFunction, frame.in_game_menu)};
				case SelectionMode::Replace:
					break;
			}
			return {Call(ReleaseMousePrimaryFunction, frame.pointer)};
		}

		NativeInputCallbacks::Target target;
		if (edge == ButtonEdge::Press)
		{
			m_focused = nullptr;
			for (const ContextButton& context : ContextButtons)
			{
				if (NativeInputCallbacks::FindRegistered(
						frame.registry.entries, frame.registry.count, context.vtable, context.focus, &target, read))
				{
					m_focused = &context;
					return {Call(context.focus, target.object)};
				}
			}
			return {};
		}
		const ContextButton* focused = m_focused;
		m_focused = nullptr;
		if (focused == nullptr ||
			!NativeInputCallbacks::FindRegistered(
				frame.registry.entries, frame.registry.count, focused->vtable, focused->hot, &target, read))
		{
			return {};
		}
		return {Call(focused->hot, target.object)};
	}

	void NativeMouseButtons::Reset()
	{
		m_focused = nullptr;
	}

	NativeMouseButtons& NativeMouseButtons::Process()
	{
		return s_process_mouse_buttons;
	}
} // namespace AVPE
