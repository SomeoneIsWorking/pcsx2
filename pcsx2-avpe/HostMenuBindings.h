// AVPE product-host menu key bindings. Fork-local; not for upstream PCSX2.
#pragma once

#include "AVPE/NativeMenuInput.h"

#include <QtCore/Qt>

#include <array>
#include <optional>
#include <string_view>

namespace AVPE::HostMenuBindings
{
	struct Binding
	{
		int key;
		NativeMenuInput::Action action;
		std::string_view label;
	};

	// The first binding of each action is the one prompts name.
	inline constexpr std::array Bindings{
		Binding{Qt::Key_Up, NativeMenuInput::Action::Up, "Up"},
		Binding{Qt::Key_W, NativeMenuInput::Action::Up, "W"},
		Binding{Qt::Key_Down, NativeMenuInput::Action::Down, "Down"},
		Binding{Qt::Key_S, NativeMenuInput::Action::Down, "S"},
		Binding{Qt::Key_Left, NativeMenuInput::Action::Left, "Left"},
		Binding{Qt::Key_A, NativeMenuInput::Action::Left, "A"},
		Binding{Qt::Key_Right, NativeMenuInput::Action::Right, "Right"},
		Binding{Qt::Key_D, NativeMenuInput::Action::Right, "D"},
		Binding{Qt::Key_Return, NativeMenuInput::Action::Activate, "Enter"},
		Binding{Qt::Key_Enter, NativeMenuInput::Action::Activate, "Enter"},
		Binding{Qt::Key_Space, NativeMenuInput::Action::Activate, "Space"},
		Binding{Qt::Key_Escape, NativeMenuInput::Action::Cancel, "Esc"},
		Binding{Qt::Key_Backspace, NativeMenuInput::Action::Cancel, "Backspace"},
	};

	constexpr std::optional<NativeMenuInput::Action> ActionForKey(const int key)
	{
		for (const Binding& binding : Bindings)
		{
			if (binding.key == key)
			{
				return binding.action;
			}
		}
		return std::nullopt;
	}

	constexpr std::string_view PromptLabel(const NativeMenuInput::Action action)
	{
		for (const Binding& binding : Bindings)
		{
			if (binding.action == action)
			{
				return binding.label;
			}
		}
		return {};
	}

	static_assert(ActionForKey(Qt::Key_W) == NativeMenuInput::Action::Up);
	static_assert(ActionForKey(Qt::Key_Down) == NativeMenuInput::Action::Down);
	static_assert(ActionForKey(Qt::Key_A) == NativeMenuInput::Action::Left);
	static_assert(ActionForKey(Qt::Key_Right) == NativeMenuInput::Action::Right);
	static_assert(ActionForKey(Qt::Key_Return) == NativeMenuInput::Action::Activate);
	static_assert(ActionForKey(Qt::Key_Escape) == NativeMenuInput::Action::Cancel);
	static_assert(!ActionForKey(Qt::Key_F12).has_value());
	static_assert(PromptLabel(NativeMenuInput::Action::Activate) == "Enter");
	static_assert(PromptLabel(NativeMenuInput::Action::Cancel) == "Esc");

	// Names every action's prompt after its bound key.
	void PublishPromptLabels();
} // namespace AVPE::HostMenuBindings
