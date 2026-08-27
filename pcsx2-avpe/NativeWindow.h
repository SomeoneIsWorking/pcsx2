// Native surface extraction for the AVPE Qt shell.
#pragma once

#include "common/WindowInfo.h"

#include <optional>

class QWidget;
class QWindow;

namespace AVPE::NativeWindow
{
	std::optional<WindowInfo> GetInfo(QWidget& widget);
	std::optional<WindowInfo> GetInfo(QWindow& window);
} // namespace AVPE::NativeWindow
