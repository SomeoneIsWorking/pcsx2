// AVPE product-host keyboard routing. Fork-local; not for upstream PCSX2.

#include "AVPE/HostInputRouter.h"

#include "AVPE/NativeMenuInput.h"

#include <lucent/log.h>

#include <QtCore/Qt>
#include <QtGui/QKeyEvent>

#include <optional>

namespace AVPE
{
	static constexpr std::optional<NativeMenuInput::Action> MenuActionForKey(const int key)
	{
		switch (key)
		{
			case Qt::Key_Up:
			case Qt::Key_W:
				return NativeMenuInput::Action::Up;
			case Qt::Key_Down:
			case Qt::Key_S:
				return NativeMenuInput::Action::Down;
			case Qt::Key_Left:
			case Qt::Key_A:
				return NativeMenuInput::Action::Left;
			case Qt::Key_Right:
			case Qt::Key_D:
				return NativeMenuInput::Action::Right;
			case Qt::Key_Return:
			case Qt::Key_Enter:
			case Qt::Key_Space:
				return NativeMenuInput::Action::Activate;
			case Qt::Key_Escape:
			case Qt::Key_Backspace:
				return NativeMenuInput::Action::Cancel;
			default:
				return std::nullopt;
		}
	}

	static_assert(MenuActionForKey(Qt::Key_W) == NativeMenuInput::Action::Up);
	static_assert(MenuActionForKey(Qt::Key_Down) == NativeMenuInput::Action::Down);
	static_assert(MenuActionForKey(Qt::Key_A) == NativeMenuInput::Action::Left);
	static_assert(MenuActionForKey(Qt::Key_Right) == NativeMenuInput::Action::Right);
	static_assert(MenuActionForKey(Qt::Key_Return) == NativeMenuInput::Action::Activate);
	static_assert(MenuActionForKey(Qt::Key_Escape) == NativeMenuInput::Action::Cancel);
	static_assert(!MenuActionForKey(Qt::Key_F12).has_value());

	bool HostInputRouter::HandleKeyPress(const QKeyEvent& event)
	{
		const std::optional<NativeMenuInput::Action> action = MenuActionForKey(event.key());
		if (!action.has_value())
			return false;
		if (event.isAutoRepeat() &&
			(*action == NativeMenuInput::Action::Activate || *action == NativeMenuInput::Action::Cancel))
			return m_consumed_keys.contains(event.key());

		const NativeMenuInput::Result result = NativeMenuInput::Apply(*action);
		if (result.Succeeded())
		{
			m_consumed_keys.insert(event.key());
			return true;
		}
		if (result.status == NativeMenuInput::Status::MenuUnavailable)
			return false;

		lucent::warn("avpe-host-input", "native menu key {} refused: {}", event.key(), result.error);
		m_consumed_keys.insert(event.key());
		return true;
	}

	bool HostInputRouter::HandleKeyRelease(const QKeyEvent& event)
	{
		if (event.isAutoRepeat())
			return m_consumed_keys.contains(event.key());
		return m_consumed_keys.erase(event.key()) != 0;
	}
} // namespace AVPE
