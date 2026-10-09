// AVPE product-host input routing. Fork-local; not for upstream PCSX2.

#include "pcsx2-avpe/HostInputRouter.h"

#include "pcsx2-avpe/HostMenuBindings.h"

#include "AVPE/NativeUnitCommands.h"
#include "AVPE/NativeCameraInput.h"
#include "AVPE/NativeMenuInput.h"
#include "AVPE/NativePromptPlacement.h"

#include <lucent/log.h>

#include <QtCore/Qt>
#include <QtGui/QKeyEvent>

#include <optional>

namespace AVPE
{
	// Held, it shows the order card as the pad's R2 does; command letters work without it.
	static constexpr int CommandCardKey = Qt::Key_Tab;
	// The unit's special ability, the top-left ability slot of a grid-hotkey RTS.
	static constexpr int SpecialKey = Qt::Key_Q;

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
				return CameraVector{-1.0f, 0.0f};
			case Qt::Key_Right:
				return CameraVector{1.0f, 0.0f};
			case Qt::Key_Up:
				return CameraVector{0.0f, -1.0f};
			case Qt::Key_Down:
				return CameraVector{0.0f, 1.0f};
			default:
				return std::nullopt;
		}
	}

	bool HostInputRouter::HandleKeyPress(const QKeyEvent& event)
	{
		if (event.key() == CommandCardKey)
		{
			if (!event.isAutoRepeat() && !NativeUnitCommands::Process().ShowCard(true))
				lucent::info("avpe-host-input", "unit command is busy; Tab ignored");
			return true;
		}
		const std::optional<NativeMenuInput::Action> action = HostMenuBindings::ActionForKey(event.key());
		if (!action.has_value())
			return HandleCommandKey(event);
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
				return HandleUnitKey(event);
			m_camera_keys.insert(event.key());
			return ApplyCameraMove(camera->x, camera->y);
		}

		lucent::warn("avpe-host-input", "native menu key {} refused: {}", event.key(), result.error);
		m_consumed_keys.insert(event.key());
		return true;
	}

	bool HostInputRouter::HandleCommandKey(const QKeyEvent& event)
	{
		const int key = event.key();
		if (key >= Qt::Key_1 && key <= Qt::Key_4)
			return NativeMenuInput::Inspect().status == NativeMenuInput::Status::MenuUnavailable &&
			       HandleUnitKey(event);
		if (key < Qt::Key_A || key > Qt::Key_Z)
			return false;
		if (event.isAutoRepeat())
			return m_consumed_keys.contains(key);
		// Qt::Key_A..Key_Z are the ASCII capitals.
		const std::optional<u32> item =
			NativePromptPlacement::Process().Capture().ItemForLetter(static_cast<char>(key));
		if (!item.has_value())
		{
			if (key == SpecialKey)
				return NativeMenuInput::Inspect().status == NativeMenuInput::Status::MenuUnavailable &&
				       HandleUnitKey(event);
			if (!NativeUnitCommands::Process().CardOrder(static_cast<char>(key)))
				return false;
			m_consumed_keys.insert(key);
			return true;
		}
		m_consumed_keys.insert(key);
		const NativeMenuInput::Result result = NativeMenuInput::ActivateItem(*item);
		if (result.Succeeded())
			return true;
		lucent::warn("avpe-host-input", "prompted item {:08x} refused key {}: {}", *item, key, result.error);
		return true;
	}

	bool HostInputRouter::HandleUnitKey(const QKeyEvent& event)
	{
		const int key = event.key();
		if (event.isAutoRepeat())
			return m_consumed_keys.contains(key);
		NativeUnitCommands& commands = NativeUnitCommands::Process();
		bool accepted = false;
		// As in StarCraft: a number recalls its group, Ctrl+number assigns it, Space jumps
		// to the latest event and Backspace to the base. The original has four groups.
		if (key >= Qt::Key_1 && key <= Qt::Key_4)
		{
			const u32 group = static_cast<u32>(key - Qt::Key_1);
			accepted = event.modifiers().testFlag(Qt::ControlModifier) ? commands.AssignGroup(group) :
			                                                             commands.RecallGroup(group);
		}
		else if (key == Qt::Key_Space)
			accepted = commands.JumpToEvent();
		else if (key == Qt::Key_Backspace)
			accepted = commands.JumpToBase();
		else if (key == SpecialKey)
			accepted = commands.UseSpecial();
		else
			return false;
		if (!accepted)
			lucent::info("avpe-host-input", "unit command is busy; key {} ignored", key);
		m_consumed_keys.insert(key);
		return true;
	}

	bool HostInputRouter::HandleKeyRelease(const QKeyEvent& event)
	{
		if (event.key() == CommandCardKey)
		{
			if (!event.isAutoRepeat())
				NativeUnitCommands::Process().ShowCard(false);
			return true;
		}
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
} // namespace AVPE
