// Native surface extraction for the AVPE Qt shell.
#include "pcsx2-avpe/NativeWindow.h"

#include <QtCore/QString>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QWidget>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <qpa/qplatformnativeinterface.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <type_traits>

namespace AVPE::NativeWindow
{
	static void* OpaqueWindowHandle(const WId window_id)
	{
		const std::uintptr_t bits = static_cast<std::uintptr_t>(window_id);
		void* handle = nullptr;
		static_assert(sizeof(bits) == sizeof(handle));
		std::memcpy(&handle, &bits, sizeof(handle));
		return handle;
	}

	template <typename T>
	static std::optional<WindowInfo> GetInfoFor(T& window)
	{
		WindowInfo info;
#if defined(_WIN32)
		info.type = WindowInfo::Type::Win32;
		info.window_handle = OpaqueWindowHandle(window.winId());
#elif defined(__APPLE__)
		info.type = WindowInfo::Type::MacOS;
		info.window_handle = OpaqueWindowHandle(window.winId());
#else
		QWindow* const handle = [&window]() {
			if constexpr (std::is_base_of_v<QWidget, T>)
				return window.windowHandle();
			else
				return &window;
		}();
		QPlatformNativeInterface* const native = QGuiApplication::platformNativeInterface();
		const QString platform = QGuiApplication::platformName();
		if (!handle || !native)
			return std::nullopt;
		if (platform == QStringLiteral("xcb"))
		{
			if (!window.isVisible())
				return std::nullopt;
			info.type = WindowInfo::Type::X11;
			info.display_connection = native->nativeResourceForWindow("display", handle);
			info.window_handle = OpaqueWindowHandle(window.winId());
		}
		else if (platform == QStringLiteral("wayland"))
		{
			info.type = WindowInfo::Type::Wayland;
			info.display_connection = native->nativeResourceForWindow("display", handle);
			info.window_handle = native->nativeResourceForWindow("surface", handle);
		}
		else
		{
			return std::nullopt;
		}
#endif

		if (!HasRequiredNativeHandles(info))
			return std::nullopt;

		const qreal scale = [&window]() {
			if constexpr (std::is_base_of_v<QWidget, T>)
				return window.devicePixelRatioF();
			else
				return window.devicePixelRatio();
		}();
		info.surface_width = static_cast<u32>(
			std::max(static_cast<int>(std::round(static_cast<qreal>(window.width()) * scale)), 1));
		info.surface_height = static_cast<u32>(
			std::max(static_cast<int>(std::round(static_cast<qreal>(window.height()) * scale)), 1));
		info.surface_scale = static_cast<float>(scale);
		const std::optional<float> queried = WindowInfo::QueryRefreshRateForWindow(info);
		const QScreen* const screen = window.screen() ? window.screen() : QGuiApplication::primaryScreen();
		info.surface_refresh_rate = queried.value_or(screen ? static_cast<float>(screen->refreshRate()) : 0.0f);
		return info;
	}

	std::optional<WindowInfo> GetInfo(QWidget& widget)
	{
		return GetInfoFor(widget);
	}

	std::optional<WindowInfo> GetInfo(QWindow& window)
	{
		return GetInfoFor(window);
	}
} // namespace AVPE::NativeWindow
