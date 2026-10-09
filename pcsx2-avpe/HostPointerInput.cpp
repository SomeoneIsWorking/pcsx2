// AVPE product-host mouse policy. Fork-local; not for upstream PCSX2.

#include "pcsx2-avpe/HostPointerInput.h"

#include <lucent/log.h>

#include <utility>

namespace AVPE
{
	namespace
	{
		NativeInput::MouseButton NativeButtonFor(const HostPointerInput::Button button)
		{
			return button == HostPointerInput::Button::Primary ? NativeInput::MouseButton::Primary :
			                                                     NativeInput::MouseButton::Secondary;
		}
	} // namespace

	HostPointerInput::Guest HostPointerInput::Guest::Live()
	{
		return {
			.inspect_menu = [] { return NativeMenuInput::Inspect(); },
			.move_menu_pointer = [](const float x, const float y) { return NativeMenuInput::MovePointerThroughDispatch(x, y); },
			.activate_menu_pointer = [] { return NativeMenuInput::ActivatePointer(); },
			.move_pointer = [](const float x, const float y) { return NativeInput::MoveAbsolute(x, y); },
			.button_edge = [](const NativeInput::MouseButton button, const NativeInput::ButtonEdge edge) { return NativeInput::ApplyButtonEdge(button, edge); },
			.camera = [](const NativeCameraInput::Action action, const float x, const float y) { return NativeCameraInput::Apply(action, x, y); },
		};
	}

	HostPointerInput::HostPointerInput(Guest guest)
		: m_guest(std::move(guest))
	{
	}

	bool HostPointerInput::Move(const float normalized_x, const float normalized_y)
	{
		const NativeMenuInput::Result menu = m_guest.inspect_menu();
		if (menu.Succeeded())
		{
			const NativeMenuInput::PointerResult result = m_guest.move_menu_pointer(normalized_x, normalized_y);
			if (result.Succeeded() || result.shuttle_status == EECallShuttle::Status::Busy)
			{
				return true;
			}
			lucent::warn("avpe-host-input", "native menu pointer move refused: {}", result.error);
			return true;
		}
		if (menu.status != NativeMenuInput::Status::MenuUnavailable)
		{
			lucent::warn("avpe-host-input", "native menu state refused pointer move: {}", menu.error);
			return true;
		}

		const NativeInput::Result result = m_guest.move_pointer(normalized_x, normalized_y);
		if (result.Succeeded())
		{
			return true;
		}
		if (result.status == NativeInput::Status::PointerUnavailable)
		{
			return false;
		}
		lucent::warn("avpe-host-input", "native gameplay pointer move refused: {}", result.error);
		return true;
	}

	bool HostPointerInput::Press(const Button button)
	{
		if (m_menu_buttons.contains(button) || m_gameplay_buttons.contains(button))
		{
			return true;
		}
		const NativeMenuInput::Result menu = m_guest.inspect_menu();
		if (menu.Succeeded())
		{
			m_menu_buttons.insert(button);
			if (button == Button::Secondary)
			{
				return true;
			}
			const NativeMenuInput::PointerResult result = m_guest.activate_menu_pointer();
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

		const NativeInput::ButtonResult result = m_guest.button_edge(NativeButtonFor(button), NativeInput::ButtonEdge::Press);
		if (result.Succeeded())
		{
			m_gameplay_buttons.insert(button);
			return true;
		}
		if (result.status == NativeInput::Status::PointerUnavailable)
		{
			return false;
		}
		lucent::warn("avpe-host-input", "native gameplay pointer press refused: {}", result.error);
		return true;
	}

	bool HostPointerInput::Release(const Button button)
	{
		if (m_suppressed_double_click_releases.erase(button) != 0)
		{
			return true;
		}
		if (m_menu_buttons.erase(button) != 0)
		{
			return true;
		}
		if (!m_gameplay_buttons.contains(button))
		{
			return false;
		}
		const NativeInput::ButtonResult result =
			m_guest.button_edge(NativeButtonFor(button), NativeInput::ButtonEdge::Release);
		if (result.Succeeded())
		{
			m_gameplay_buttons.erase(button);
		}
		else
		{
			lucent::warn("avpe-host-input", "native gameplay pointer release refused: {}", result.error);
		}
		return true;
	}

	bool HostPointerInput::DoubleClick(const Button button)
	{
		m_suppressed_double_click_releases.insert(button);
		return true;
	}

	bool HostPointerInput::Wheel(const float steps)
	{
		if (steps == 0.0f)
		{
			return false;
		}
		const NativeMenuInput::Result menu = m_guest.inspect_menu();
		if (menu.status != NativeMenuInput::Status::MenuUnavailable)
		{
			return true;
		}
		const NativeCameraInput::Result result = m_guest.camera(NativeCameraInput::Action::Zoom, steps, 0.0f);
		if (result.Succeeded())
		{
			return true;
		}
		if (result.status == NativeCameraInput::Status::CameraUnavailable)
		{
			return false;
		}
		lucent::warn("avpe-host-input", "native camera zoom refused: {}", result.error);
		return true;
	}
} // namespace AVPE
