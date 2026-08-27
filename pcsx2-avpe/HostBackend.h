// AVPE host-to-emulator boundary. Fork-local; independent of the PCSX2 GUI.

#pragma once

#include "common/WindowInfo.h"

#include <optional>

class QWindow;
class QWidget;

namespace AVPE
{
	class HostWindow;
	class RenderSurface;

	class HostBackend
	{
	public:
		virtual ~HostBackend() = default;

		virtual void ConnectWindow(HostWindow& window) = 0;
		virtual void ConnectRenderSurface(RenderSurface& surface) = 0;
		virtual std::optional<WindowInfo> GetWindowInfo(QWindow& window) const = 0;
		virtual std::optional<WindowInfo> GetWindowInfo(QWidget& window) const = 0;
		virtual void ResizeWindow(QWidget& window, s32 width, s32 height) const = 0;
		virtual void RequestExit() = 0;
	};
} // namespace AVPE
