// Standalone AVPE frontend composition around the PCSX2 core library.
#include "pcsx2-avpe/Runtime.h"

#include "pcsx2-avpe/EmulationThread.h"
#include "pcsx2-avpe/HostWindow.h"
#include "pcsx2-avpe/NativeWindow.h"
#include "pcsx2-avpe/RenderSurface.h"

#include "pcsx2/MTGS.h"
#include "pcsx2/VMManager.h"

#include "common/Assertions.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace AVPE
{
	static Runtime* s_runtime = nullptr;

	Runtime::Runtime(QApplication& application)
		: m_emulation_thread(std::make_unique<EmulationThread>(application.thread()))
		, m_window(std::make_unique<HostWindow>(*this))
	{
		pxAssert(!s_runtime);
		s_runtime = this;
	}

	Runtime::~Runtime()
	{
		Stop();
		m_window.reset();
		s_runtime = nullptr;
	}

	Runtime* Runtime::Get()
	{
		return s_runtime;
	}

	void Runtime::RequestApplicationExit(const int exit_code)
	{
		QMetaObject::invokeMethod(QCoreApplication::instance(), [exit_code]() { QCoreApplication::exit(exit_code); }, Qt::QueuedConnection);
	}

	void Runtime::Start(VMBootParameters boot_parameters)
	{
		boot_parameters.fullscreen.value_or(false) ? m_window->showFullScreen() : m_window->show();
		m_window->raise();
		m_window->activateWindow();
		m_emulation_thread->Start(std::move(boot_parameters));
	}

	void Runtime::Stop()
	{
		if (m_emulation_thread)
			m_emulation_thread->Stop();
	}

	EmulationThread& Runtime::GetEmulationThread()
	{
		return *m_emulation_thread;
	}

	std::optional<WindowInfo> Runtime::AcquireRenderWindow(const bool recreate_window)
	{
		std::optional<WindowInfo> result;
		QMetaObject::invokeMethod(m_window.get(), [this, recreate_window, &result]() { result = m_window->AcquireRenderWindow(
																						   recreate_window, m_window->isFullScreen(), true, false); }, Qt::BlockingQueuedConnection);
		return result;
	}

	void Runtime::ReleaseRenderWindow()
	{
		QMetaObject::invokeMethod(m_window.get(), &HostWindow::ReleaseRenderWindow,
			Qt::BlockingQueuedConnection);
	}

	std::optional<WindowInfo> Runtime::GetTopLevelWindowInfo()
	{
		std::optional<WindowInfo> result;
		QMetaObject::invokeMethod(m_window.get(), [this, &result]() { result = m_window->getWindowInfo(); }, Qt::BlockingQueuedConnection);
		return result;
	}

	void Runtime::RequestResize(const s32 width, const s32 height)
	{
		QMetaObject::invokeMethod(m_window.get(), [this, width, height]() { m_window->ResizeRenderWindow(width, height); }, Qt::QueuedConnection);
	}

	void Runtime::RequestMouseMode(const bool relative_mode, const bool hide_cursor)
	{
		QMetaObject::invokeMethod(m_window.get(), [this, relative_mode, hide_cursor]() { m_window->SetMouseMode(relative_mode, hide_cursor); }, Qt::QueuedConnection);
	}

	void Runtime::RequestMouseLock(const bool locked)
	{
		QMetaObject::invokeMethod(m_window.get(), [this, locked]() { m_window->SetMouseLock(locked); }, Qt::QueuedConnection);
	}

	bool Runtime::IsFullscreen() const
	{
		return m_window->isFullScreen();
	}

	void Runtime::SetFullscreen(const bool fullscreen)
	{
		QMetaObject::invokeMethod(m_window.get(), [this, fullscreen]() { fullscreen ? m_window->showFullScreen() : m_window->showNormal(); }, Qt::QueuedConnection);
	}

	void Runtime::ConnectWindow(HostWindow& window)
	{
		Q_UNUSED(window);
	}

	void Runtime::ConnectRenderSurface(RenderSurface& surface)
	{
		connect(&surface, &RenderSurface::WindowResized, m_emulation_thread.get(),
			[](const u32 width, const u32 height, const float scale) {
				if (MTGS::IsOpen())
					MTGS::ResizeDisplayWindow(width, height, scale);
			});
		connect(&surface, &RenderSurface::WindowRestored, m_emulation_thread.get(), []() {
			if (VMManager::HasValidVM() && VMManager::GetState() != VMState::Running)
				MTGS::PresentCurrentFrame();
		});
	}

	std::optional<WindowInfo> Runtime::GetWindowInfo(QWindow& window) const
	{
		return NativeWindow::GetInfo(window);
	}

	std::optional<WindowInfo> Runtime::GetWindowInfo(QWidget& window) const
	{
		return NativeWindow::GetInfo(window);
	}

	void Runtime::ResizeWindow(QWidget& window, const s32 width, const s32 height) const
	{
		window.resize(width, height);
	}

	void Runtime::RequestExit()
	{
		m_emulation_thread->RequestShutdown();
	}
} // namespace AVPE
