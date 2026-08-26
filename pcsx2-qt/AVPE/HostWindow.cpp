// AVPE-owned product window. Fork-local; not for upstream PCSX2.
#include "AVPE/HostWindow.h"

#include "DisplayWidget.h"
#include "MainWindow.h"
#include "QtHost.h"
#include "QtUtils.h"

#include "common/Assertions.h"

#include <lucent/log.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtGui/QCloseEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QWidget>

#include <algorithm>

namespace AVPE
{
	HostWindow* g_host_window = nullptr;

	HostWindow::HostWindow(QWidget* parent)
		: QMainWindow(parent)
	{
		pxAssert(!g_host_window);
		g_host_window = this;
		setWindowTitle(QStringLiteral("Aliens Versus Predator: Extinction"));
		resize(960, 672);
	}

	HostWindow::~HostWindow()
	{
		destroyRenderSurface();
		g_host_window = nullptr;
	}

	void HostWindow::connectEmulationDisplay(EmuThread* thread)
	{
		connect(thread, &EmuThread::onAcquireRenderWindowRequested, this, &HostWindow::acquireRenderWindow,
			Qt::BlockingQueuedConnection);
		connect(thread, &EmuThread::onReleaseRenderWindowRequested, this, &HostWindow::releaseRenderWindow,
			Qt::BlockingQueuedConnection);
		connect(thread, &EmuThread::onResizeRenderWindowRequested, this, &HostWindow::resizeRenderWindow);
		connect(thread, &EmuThread::onMouseModeRequested, this, &HostWindow::setMouseMode);
		connect(thread, &EmuThread::onMouseLockRequested, this, &HostWindow::setMouseLock);
	}

	std::optional<WindowInfo> HostWindow::getWindowInfo() const
	{
		return QtUtils::GetWindowInfoForWindow(this);
	}

	std::optional<WindowInfo> HostWindow::acquireRenderWindow(
		bool recreate_window, bool fullscreen, bool render_to_main, bool surfaceless)
	{
		Q_UNUSED(render_to_main);
		if (surfaceless)
		{
			destroyRenderSurface();
			return WindowInfo();
		}

		if (!m_surface || recreate_window)
		{
			destroyRenderSurface();
			createRenderSurface(fullscreen);
		}
		else if (fullscreen != isFullScreen())
		{
			fullscreen ? showFullScreen() : showNormal();
		}

		m_surface->setFocus();
		return m_surface->getWindowInfo();
	}

	void HostWindow::createRenderSurface(bool fullscreen)
	{
		m_surface = new DisplaySurface();
		m_surface_container = m_surface->createWindowContainer(this);
		m_surface->installEventFilter(this);
		m_surface_container->installEventFilter(this);
		setCentralWidget(m_surface_container);
		g_emu_thread->connectDisplaySignals(m_surface);
		fullscreen ? showFullScreen() : showNormal();
		QGuiApplication::sync();
	}

	void HostWindow::releaseRenderWindow()
	{
		destroyRenderSurface();
	}

	void HostWindow::destroyRenderSurface()
	{
		if (!m_surface_container)
			return;

		takeCentralWidget();
		m_surface_container->deleteLater();
		m_surface_container = nullptr;
		m_surface = nullptr;
	}

	void HostWindow::resizeRenderWindow(qint32 width, qint32 height)
	{
		if (width > 0 && height > 0 && !isFullScreen())
			QtUtils::ResizePotentiallyFixedSizeWindow(this, width, height);
	}

	void HostWindow::setMouseMode(bool relative_mode, bool hide_cursor)
	{
		if (!m_surface)
			return;
		m_surface->updateRelativeMode(relative_mode);
		m_surface->updateCursor(hide_cursor);
	}

	void HostWindow::setMouseLock(bool locked)
	{
		lucent::info("avpe-host", "mouse lock request: {}", locked);
	}

	void HostWindow::closeEvent(QCloseEvent* event)
	{
		if (!m_closing)
		{
			m_closing = true;
			QMetaObject::invokeMethod(g_main_window, "requestExit", Qt::QueuedConnection, Q_ARG(bool, false));
		}
		event->ignore();
	}

	bool HostWindow::eventFilter(QObject* watched, QEvent* event)
	{
		if (watched == m_surface || watched == m_surface_container)
		{
			if (event->type() == QEvent::KeyPress &&
				m_input_router.HandleKeyPress(*static_cast<QKeyEvent*>(event)))
			{
				return true;
			}
			if (event->type() == QEvent::KeyRelease &&
				m_input_router.HandleKeyRelease(*static_cast<QKeyEvent*>(event)))
			{
				return true;
			}
			if (watched == m_surface && event->type() == QEvent::MouseMove)
			{
				const QMouseEvent* const mouse_event = static_cast<QMouseEvent*>(event);
				const float width = static_cast<float>(std::max(m_surface->width() - 1, 1));
				const float height = static_cast<float>(std::max(m_surface->height() - 1, 1));
				const float normalized_x =
					std::clamp(static_cast<float>(mouse_event->position().x()) / width, 0.0f, 1.0f);
				const float normalized_y =
					std::clamp(static_cast<float>(mouse_event->position().y()) / height, 0.0f, 1.0f);
				if (m_input_router.HandlePointerMove(normalized_x, normalized_y))
					return true;
			}
			if (watched == m_surface &&
				(event->type() == QEvent::MouseButtonPress ||
					event->type() == QEvent::MouseButtonRelease ||
					event->type() == QEvent::MouseButtonDblClick))
			{
				const QMouseEvent* const mouse_event = static_cast<QMouseEvent*>(event);
				std::optional<HostInputRouter::PointerButton> button;
				if (mouse_event->button() == Qt::LeftButton)
					button = HostInputRouter::PointerButton::Primary;
				else if (mouse_event->button() == Qt::RightButton)
					button = HostInputRouter::PointerButton::Secondary;
				if (button.has_value())
				{
					if (event->type() == QEvent::MouseButtonDblClick)
						return m_input_router.HandlePointerDoubleClick(*button);
					return m_input_router.HandlePointerButton(
						*button, event->type() == QEvent::MouseButtonPress);
				}
			}
		}
		return QMainWindow::eventFilter(watched, event);
	}
} // namespace AVPE
