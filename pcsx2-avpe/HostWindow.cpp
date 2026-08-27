// AVPE-owned product window. Fork-local; independent of the PCSX2 GUI.
#include "pcsx2-avpe/HostWindow.h"

#include "pcsx2-avpe/HostBackend.h"
#include "pcsx2-avpe/RenderSurface.h"

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

	HostWindow::HostWindow(HostBackend& backend, QWidget* parent)
		: QMainWindow(parent)
		, m_backend(backend)
	{
		pxAssert(!g_host_window);
		g_host_window = this;
		setWindowTitle(QStringLiteral("Aliens Versus Predator: Extinction"));
		resize(960, 672);
		m_backend.ConnectWindow(*this);
	}

	HostWindow::~HostWindow()
	{
		destroyRenderSurface();
		g_host_window = nullptr;
	}

	std::optional<WindowInfo> HostWindow::getWindowInfo()
	{
		return m_backend.GetWindowInfo(*this);
	}

	std::optional<WindowInfo> HostWindow::AcquireRenderWindow(
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

		m_surface->SetFocus();
		return m_backend.GetWindowInfo(*m_surface);
	}

	void HostWindow::createRenderSurface(bool fullscreen)
	{
		m_surface = new RenderSurface();
		m_surface_container = m_surface->CreateContainer(this);
		m_surface->installEventFilter(this);
		m_surface_container->installEventFilter(this);
		setCentralWidget(m_surface_container);
		m_backend.ConnectRenderSurface(*m_surface);
		fullscreen ? showFullScreen() : showNormal();
		QGuiApplication::sync();
	}

	void HostWindow::ReleaseRenderWindow()
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

	void HostWindow::ResizeRenderWindow(qint32 width, qint32 height)
	{
		if (width > 0 && height > 0 && !isFullScreen())
			m_backend.ResizeWindow(*this, width, height);
	}

	void HostWindow::SetMouseMode(bool relative_mode, bool hide_cursor)
	{
		if (!m_surface)
			return;
		m_surface->SetMouseMode(relative_mode, hide_cursor);
	}

	void HostWindow::SetMouseLock(bool locked)
	{
		lucent::info("avpe-host", "mouse lock request: {}", locked);
	}

	void HostWindow::closeEvent(QCloseEvent* event)
	{
		if (!m_closing)
		{
			m_closing = true;
			m_backend.RequestExit();
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
