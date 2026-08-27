// Standalone AVPE frontend composition around the PCSX2 core library.
#pragma once

#include "pcsx2-avpe/HostBackend.h"
#include "pcsx2/VMManager.h"

#include <QtCore/QObject>

#include <memory>
#include <optional>

class QApplication;
class QWidget;
class QWindow;

namespace AVPE
{
	class EmulationThread;
	class HostWindow;
	class RenderSurface;

	class Runtime final : public QObject, public HostBackend
	{
		Q_OBJECT

	public:
		explicit Runtime(QApplication& application);
		~Runtime() override;

		static Runtime* Get();
		static void RequestApplicationExit(int exit_code);

		void Start(VMBootParameters boot_parameters);
		void Stop();
		EmulationThread& GetEmulationThread();

		std::optional<WindowInfo> AcquireRenderWindow(bool recreate_window);
		void ReleaseRenderWindow();
		std::optional<WindowInfo> GetTopLevelWindowInfo();
		void RequestResize(s32 width, s32 height);
		void RequestMouseMode(bool relative_mode, bool hide_cursor);
		void RequestMouseLock(bool locked);
		bool IsFullscreen() const;
		void SetFullscreen(bool fullscreen);

		void ConnectWindow(HostWindow& window) override;
		void ConnectRenderSurface(RenderSurface& surface) override;
		std::optional<WindowInfo> GetWindowInfo(QWindow& window) const override;
		std::optional<WindowInfo> GetWindowInfo(QWidget& window) const override;
		void ResizeWindow(QWidget& window, s32 width, s32 height) const override;
		void RequestExit() override;

	private:
		std::unique_ptr<EmulationThread> m_emulation_thread;
		std::unique_ptr<HostWindow> m_window;
	};
} // namespace AVPE
