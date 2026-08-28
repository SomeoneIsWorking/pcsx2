// AVPE-owned product window. Fork-local; independent of the PCSX2 GUI.
#pragma once

#include "pcsx2-avpe/HostInputRouter.h"

#include "common/WindowInfo.h"

#include <QtCore/QTimer>
#include <QtWidgets/QMainWindow>

#include <optional>

class QCloseEvent;
class QEvent;
class QObject;
class QWidget;

namespace AVPE
{
	class HostBackend;
	class RenderSurface;

	class HostWindow final : public QMainWindow
	{
		Q_OBJECT

	public:
		explicit HostWindow(HostBackend& backend, QWidget* parent = nullptr);
		~HostWindow() override;

		std::optional<WindowInfo> getWindowInfo();

	public Q_SLOTS:
		std::optional<WindowInfo> AcquireRenderWindow(
			bool recreate_window, bool fullscreen, bool render_to_main, bool surfaceless);
		void ReleaseRenderWindow();
		void ResizeRenderWindow(qint32 width, qint32 height);
		void SetMouseMode(bool relative_mode, bool hide_cursor);
		void SetMouseLock(bool locked);

	protected:
		void closeEvent(QCloseEvent* event) override;
		bool eventFilter(QObject* watched, QEvent* event) override;

	private:
		void createRenderSurface(bool fullscreen);
		void destroyRenderSurface();

		HostBackend& m_backend;
		RenderSurface* m_surface = nullptr;
		QWidget* m_surface_container = nullptr;
		HostInputRouter m_input_router;
		QTimer m_input_timer;
		bool m_closing = false;
	};

	extern HostWindow* g_host_window;
} // namespace AVPE
