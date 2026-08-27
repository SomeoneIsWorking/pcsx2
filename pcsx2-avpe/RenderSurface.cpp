// AVPE presentation surface. Fork-local; independent of PCSX2's DisplaySurface.

#include "pcsx2-avpe/RenderSurface.h"

#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QResizeEvent>
#include <QtGui/QWindowStateChangeEvent>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <cmath>

namespace AVPE
{
	RenderSurface::RenderSurface()
	{
		m_resize_timer.setSingleShot(true);
		m_resize_timer.setTimerType(Qt::PreciseTimer);
		connect(&m_resize_timer, &QTimer::timeout, this,
			[this]() { emit WindowResized(m_surface_width, m_surface_height, m_surface_scale); });
	}

	QWidget* RenderSurface::CreateContainer(QWidget* parent)
	{
		m_container = QWidget::createWindowContainer(this, parent);
		m_container->setFocusPolicy(Qt::StrongFocus);
		return m_container;
	}

	void RenderSurface::SetFocus()
	{
		m_container ? m_container->setFocus() : requestActivate();
	}

	void RenderSurface::SetMouseMode(const bool relative, const bool hidden)
	{
		if (m_hidden != hidden)
		{
			m_hidden = hidden;
			hidden ? setCursor(Qt::BlankCursor) : unsetCursor();
		}
		if (m_relative == relative)
			return;

		m_relative = relative;
		if (relative)
		{
			m_relative_start = QCursor::pos();
			UpdateCenterPosition();
			setMouseGrabEnabled(true);
		}
		else
		{
			setMouseGrabEnabled(false);
			QCursor::setPos(m_relative_start);
		}
	}

	void RenderSurface::UpdateCenterPosition()
	{
		if (!m_relative)
			return;
		m_relative_center = mapToGlobal(QPoint((width() + 1) / 2, (height() + 1) / 2));
		QCursor::setPos(m_relative_center);
		m_relative_center = QCursor::pos();
	}

	void RenderSurface::RecordSurfaceSize()
	{
		const qreal scale = devicePixelRatio();
		const u32 width = static_cast<u32>(
			std::max(static_cast<int>(std::round(static_cast<qreal>(this->width()) * scale)), 1));
		const u32 height = static_cast<u32>(
			std::max(static_cast<int>(std::round(static_cast<qreal>(this->height()) * scale)), 1));
		if (width == m_surface_width && height == m_surface_height &&
			static_cast<float>(scale) == m_surface_scale)
		{
			return;
		}

		m_surface_width = width;
		m_surface_height = height;
		m_surface_scale = static_cast<float>(scale);
		m_resize_timer.start(100);
	}

	bool RenderSurface::event(QEvent* event)
	{
		switch (event->type())
		{
			case QEvent::DevicePixelRatioChange:
			case QEvent::Resize:
				QWindow::event(event);
				RecordSurfaceSize();
				UpdateCenterPosition();
				return true;
			case QEvent::Move:
				UpdateCenterPosition();
				break;
			case QEvent::WindowStateChange:
				if (static_cast<QWindowStateChangeEvent*>(event)->oldState() & Qt::WindowMinimized)
					emit WindowRestored();
				break;
			default:
				break;
		}
		return QWindow::event(event);
	}
} // namespace AVPE
