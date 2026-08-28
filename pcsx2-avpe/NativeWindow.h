// Native surface extraction for the AVPE Qt shell.
#pragma once

#include "common/WindowInfo.h"

#include <optional>

class QWidget;
class QWindow;

namespace AVPE::NativeWindow
{
	constexpr bool HasRequiredNativeHandles(const WindowInfo& info)
	{
		switch (info.type)
		{
			case WindowInfo::Type::Surfaceless:
				return true;
			case WindowInfo::Type::X11:
			case WindowInfo::Type::Wayland:
				return info.display_connection != nullptr && info.window_handle != nullptr;
			case WindowInfo::Type::Win32:
			case WindowInfo::Type::MacOS:
				return info.window_handle != nullptr;
		}
		return false;
	}

	std::optional<WindowInfo> GetInfo(QWidget& widget);
	std::optional<WindowInfo> GetInfo(QWindow& window);
} // namespace AVPE::NativeWindow
