// AVPE presentation surface. Fork-local; independent of PCSX2's DisplaySurface.

#pragma once

#include "common/WindowInfo.h"

#include <QtCore/QTimer>
#include <QtGui/QWindow>

class QWidget;

namespace AVPE
{
	class RenderSurface final : public QWindow
	{
		Q_OBJECT

	public:
		RenderSurface();

		QWidget* CreateContainer(QWidget* parent);
		void SetFocus();
		void SetMouseMode(bool relative, bool hidden);

	Q_SIGNALS:
		void WindowResized(u32 width, u32 height, float scale);
		void WindowRestored();

	protected:
		bool event(QEvent* event) override;

	private:
		void UpdateCenterPosition();
		void RecordSurfaceSize();

		QTimer m_resize_timer;
		QWidget* m_container = nullptr;
		QPoint m_relative_start;
		QPoint m_relative_center;
		u32 m_surface_width = 0;
		u32 m_surface_height = 0;
		float m_surface_scale = 1.0f;
		bool m_relative = false;
		bool m_hidden = false;
	};
} // namespace AVPE
