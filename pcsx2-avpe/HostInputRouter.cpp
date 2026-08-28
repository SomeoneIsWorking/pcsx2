// AVPE product-host input routing. Fork-local; not for upstream PCSX2.

#include "pcsx2-avpe/HostInputRouter.h"

#include "AVPE/NativeInput.h"
#include "AVPE/NativeCameraInput.h"
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

	struct CameraVector
	{
		float x;
		float y;
	};

	static constexpr std::optional<CameraVector> CameraMoveForKey(const int key)
	{
		switch (key)
		{
			case Qt::Key_Left:
			case Qt::Key_A:
				return CameraVector{-1.0f, 0.0f};
			case Qt::Key_Right:
			case Qt::Key_D:
				return CameraVector{1.0f, 0.0f};
			case Qt::Key_Up:
			case Qt::Key_W:
				return CameraVector{0.0f, -1.0f};
			case Qt::Key_Down:
			case Qt::Key_S:
				return CameraVector{0.0f, 1.0f};
			default:
				return std::nullopt;
		}
	}

	bool HostInputRouter::HandleKeyPress(const QKeyEvent& event)
	{
		const std::optional<NativeMenuInput::Action> action = MenuActionForKey(event.key());
		if (!action.has_value())
			return false;
		if (m_camera_keys.contains(event.key()))
			return true;
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
		{
			const std::optional<CameraVector> camera = CameraMoveForKey(event.key());
			if (!camera.has_value())
				return false;
			m_camera_keys.insert(event.key());
			return ApplyCameraMove(camera->x, camera->y);
		}

		lucent::warn("avpe-host-input", "native menu key {} refused: {}", event.key(), result.error);
		m_consumed_keys.insert(event.key());
		return true;
	}

	bool HostInputRouter::HandleKeyRelease(const QKeyEvent& event)
	{
		if (event.isAutoRepeat())
			return m_consumed_keys.contains(event.key()) || m_camera_keys.contains(event.key());
		if (m_camera_keys.erase(event.key()) != 0)
			return true;
		return m_consumed_keys.erase(event.key()) != 0;
	}

	bool HostInputRouter::ApplyCameraMove(const float x, const float y)
	{
		const NativeCameraInput::Result result = NativeCameraInput::Apply(
			NativeCameraInput::Action::Move, x, y);
		if (result.Succeeded() || result.status == NativeCameraInput::Status::CameraUnavailable)
			return true;
		lucent::warn("avpe-host-input", "native camera move refused: {}", result.error);
		return true;
	}

	bool HostInputRouter::ApplyCameraZoom(const float steps)
	{
		const NativeCameraInput::Result result = NativeCameraInput::Apply(
			NativeCameraInput::Action::Zoom, steps);
		if (result.Succeeded())
			return true;
		if (result.status == NativeCameraInput::Status::CameraUnavailable)
			return false;
		lucent::warn("avpe-host-input", "native camera zoom refused: {}", result.error);
		return true;
	}

	bool HostInputRouter::HandleWheel(const float steps)
	{
		if (steps == 0.0f)
			return false;
		const NativeMenuInput::Result menu = NativeMenuInput::Inspect();
		if (menu.status != NativeMenuInput::Status::MenuUnavailable)
			return true;
		return ApplyCameraZoom(steps);
	}

	void HostInputRouter::Tick()
	{
		float x = 0.0f;
		float y = 0.0f;
		for (const int key : m_camera_keys)
		{
			const std::optional<CameraVector> camera = CameraMoveForKey(key);
			if (camera.has_value())
			{
				x += camera->x;
				y += camera->y;
			}
		}
		if (x != 0.0f || y != 0.0f)
		{
			const NativeMenuInput::Result menu = NativeMenuInput::Inspect();
			if (menu.status == NativeMenuInput::Status::MenuUnavailable)
				ApplyCameraMove(x, y);
		}
	}

	static NativeInput::MouseButton NativeButtonFor(
		const HostInputRouter::PointerButton button)
	{
		return button == HostInputRouter::PointerButton::Primary ? NativeInput::MouseButton::Primary :
		                                                           NativeInput::MouseButton::Secondary;
	}

	bool HostInputRouter::HandlePointerMove(const float normalized_x, const float normalized_y)
	{
		const NativeMenuInput::Result menu = NativeMenuInput::Inspect();
		if (menu.Succeeded())
		{
			const NativeMenuInput::PointerResult result =
				NativeMenuInput::MovePointer(normalized_x, normalized_y);
			if (result.Succeeded() || result.shuttle_status == EECallShuttle::Status::Busy)
				return true;
			lucent::warn("avpe-host-input", "native menu pointer move refused: {}", result.error);
			return true;
		}
		if (menu.status != NativeMenuInput::Status::MenuUnavailable)
		{
			lucent::warn("avpe-host-input", "native menu state refused pointer move: {}", menu.error);
			return true;
		}

		const NativeInput::Result result = NativeInput::MoveAbsolute(normalized_x, normalized_y);
		if (result.Succeeded())
			return true;
		if (result.status == NativeInput::Status::PointerUnavailable)
			return false;
		lucent::warn("avpe-host-input", "native gameplay pointer move refused: {}", result.error);
		return true;
	}

	bool HostInputRouter::HandlePointerButton(const PointerButton button, const bool pressed)
	{
		if (!pressed)
		{
			if (m_suppressed_double_click_releases.erase(button) != 0)
				return true;
			if (m_menu_pointer_buttons.erase(button) != 0)
				return true;
			if (!m_gameplay_pointer_buttons.contains(button))
				return false;

			const NativeInput::ButtonResult result =
				NativeInput::ApplyButtonEdge(NativeButtonFor(button), NativeInput::ButtonEdge::Release);
			if (result.Succeeded())
				m_gameplay_pointer_buttons.erase(button);
			else
				lucent::warn("avpe-host-input", "native gameplay pointer release refused: {}", result.error);
			return true;
		}

		if (m_menu_pointer_buttons.contains(button) || m_gameplay_pointer_buttons.contains(button))
			return true;
		const NativeMenuInput::Result menu = NativeMenuInput::Inspect();
		if (menu.Succeeded())
		{
			m_menu_pointer_buttons.insert(button);
			if (button == PointerButton::Secondary)
				return true;

			const NativeMenuInput::PointerResult result = NativeMenuInput::ActivatePointer();
			if (result.Succeeded() || result.status == NativeMenuInput::Status::FocusUnavailable ||
				result.shuttle_status == EECallShuttle::Status::Busy)
			{
				return true;
			}
			lucent::warn("avpe-host-input", "native menu pointer activation refused: {}", result.error);
			return true;
		}
		if (menu.status != NativeMenuInput::Status::MenuUnavailable)
		{
			lucent::warn("avpe-host-input", "native menu state refused pointer press: {}", menu.error);
			return true;
		}

		const NativeInput::ButtonResult result =
			NativeInput::ApplyButtonEdge(NativeButtonFor(button), NativeInput::ButtonEdge::Press);
		if (result.Succeeded())
		{
			m_gameplay_pointer_buttons.insert(button);
			return true;
		}
		if (result.status == NativeInput::Status::PointerUnavailable)
			return false;
		lucent::warn("avpe-host-input", "native gameplay pointer press refused: {}", result.error);
		return true;
	}

	bool HostInputRouter::HandlePointerDoubleClick(const PointerButton button)
	{
		m_suppressed_double_click_releases.insert(button);
		return true;
	}
} // namespace AVPE
