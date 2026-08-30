// AVPE frontend ownership of the PCSX2 core lifecycle.
#include "pcsx2-avpe/EmulationThread.h"

#include "pcsx2-avpe/Runtime.h"

#include "pcsx2/VMManager.h"

#include "common/Error.h"

#include <lucent/log.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QMetaObject>
#include <QtCore/QtGlobal>

namespace AVPE
{
	EmulationThread::EmulationThread(QThread* ui_thread)
		: m_ui_thread(ui_thread)
	{
		setStackSize(VMManager::EMU_THREAD_STACK_SIZE);
	}

	EmulationThread::~EmulationThread() = default;

	void EmulationThread::Start(VMBootParameters boot_parameters)
	{
		m_boot_parameters = std::make_unique<VMBootParameters>(std::move(boot_parameters));
		QThread::start();
		m_started.acquire();
		moveToThread(this);
	}

	void EmulationThread::Stop()
	{
		if (!isRunning())
			return;
		QMetaObject::invokeMethod(this, [this]() { StopInThread(); }, Qt::QueuedConnection);
		wait();
	}

	void EmulationThread::Run(std::function<void()> function, const bool block)
	{
		if (IsCurrentThread())
		{
			function();
			return;
		}
		QMetaObject::invokeMethod(this, std::move(function),
			block ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
	}

	void EmulationThread::PumpEvents()
	{
		Q_ASSERT(IsCurrentThread());
		if (m_event_loop)
			m_event_loop->processEvents(QEventLoop::AllEvents);
	}

	void EmulationThread::Wake()
	{
		Run([this]() {
			if (m_event_loop)
				m_event_loop->quit();
		},
			false);
	}

	bool EmulationThread::IsCurrentThread() const
	{
		return QThread::currentThread() == this;
	}

	void EmulationThread::RequestShutdown()
	{
		if (!IsCurrentThread())
		{
			QMetaObject::invokeMethod(this, &EmulationThread::RequestShutdown, Qt::QueuedConnection);
			return;
		}

		const VMState state = VMManager::GetState();
		if (state == VMState::Paused && m_event_loop)
			m_event_loop->quit();
		if (state != VMState::Shutdown)
			VMManager::SetState(VMState::Stopping);
		else
			StopInThread();
	}

	void EmulationThread::StopInThread()
	{
		if (VMManager::HasValidVM())
			VMManager::Shutdown(false);
		m_stop_requested.store(true, std::memory_order_release);
		if (m_event_loop)
			m_event_loop->quit();
	}

	void EmulationThread::run()
	{
		QEventLoop event_loop;
		m_event_loop = &event_loop;
		m_started.release();

		if (!VMManager::Internal::CPUThreadInitialize())
		{
			lucent::error("avpe-core", "PCSX2 CPU-thread initialization failed");
			VMManager::Internal::CPUThreadShutdown();
			Runtime::RequestApplicationExit(EXIT_FAILURE);
			m_event_loop = nullptr;
			return;
		}

		Error error;
		const VMBootResult boot_result = VMManager::Initialize(*m_boot_parameters, &error);
		m_boot_parameters.reset();
		if (boot_result != VMBootResult::StartupSuccess)
		{
			lucent::error("avpe-core", "game boot failed: {}", error.GetDescription());
			Runtime::RequestApplicationExit(EXIT_FAILURE);
			m_stop_requested.store(true, std::memory_order_release);
		}
		else
		{
			VMManager::SetState(VMState::Running);
		}

		while (!m_stop_requested.load(std::memory_order_acquire))
		{
			switch (VMManager::GetState())
			{
				case VMState::Shutdown:
				case VMState::Paused:
					event_loop.exec();
					break;
				case VMState::Running:
					event_loop.processEvents(QEventLoop::AllEvents);
					VMManager::Execute();
					break;
				case VMState::Resetting:
					VMManager::Reset();
					break;
				case VMState::Stopping:
					VMManager::Shutdown(false);
					break;
				case VMState::Initializing:
					break;
			}
		}

		VMManager::Internal::CPUThreadShutdown();
		m_event_loop = nullptr;
		moveToThread(m_ui_thread);
	}
} // namespace AVPE
