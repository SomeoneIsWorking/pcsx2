// AVPE-owned product window. Fork-local; not for upstream PCSX2.
#pragma once

#include "AVPE/HostInputRouter.h"

#include "common/WindowInfo.h"

#include <QtWidgets/QMainWindow>

#include <optional>

class DisplaySurface;
class EmuThread;
class QCloseEvent;
class QEvent;
class QObject;
class QWidget;

namespace AVPE
{
	class HostWindow final : public QMainWindow
	{
		Q_OBJECT

	public:
		explicit HostWindow(QWidget* parent = nullptr);
		~HostWindow() override;

		void connectEmulationDisplay(EmuThread* thread);
		std::optional<WindowInfo> getWindowInfo() const;

	private Q_SLOTS:
		std::optional<WindowInfo> acquireRenderWindow(bool recreate_window, bool fullscreen, bool render_to_main, bool surfaceless);
		void releaseRenderWindow();
		void resizeRenderWindow(qint32 width, qint32 height);
		void setMouseMode(bool relative_mode, bool hide_cursor);
		void setMouseLock(bool locked);

	protected:
		void closeEvent(QCloseEvent* event) override;
		bool eventFilter(QObject* watched, QEvent* event) override;

	private:
		void createRenderSurface(bool fullscreen);
		void destroyRenderSurface();

		DisplaySurface* m_surface = nullptr;
		QWidget* m_surface_container = nullptr;
		HostInputRouter m_input_router;
		bool m_closing = false;
	};

	extern HostWindow* g_host_window;
} // namespace AVPE
