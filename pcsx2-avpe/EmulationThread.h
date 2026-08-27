// AVPE frontend ownership of the PCSX2 core lifecycle.
#pragma once

#include "pcsx2/VMManager.h"

#include <QtCore/QSemaphore>
#include <QtCore/QThread>

#include <atomic>
#include <functional>
#include <memory>

class QEventLoop;

namespace AVPE
{
	class EmulationThread final : public QThread
	{
		Q_OBJECT

	public:
		explicit EmulationThread(QThread* ui_thread);
		~EmulationThread() override;

		void Start(VMBootParameters boot_parameters);
		void Stop();
		void Run(std::function<void()> function, bool block);
		void Wake();
		bool IsCurrentThread() const;

	public Q_SLOTS:
		void RequestShutdown();

	protected:
		void run() override;

	private:
		void StopInThread();

		QThread* m_ui_thread;
		QSemaphore m_started;
		QEventLoop* m_event_loop = nullptr;
		std::unique_ptr<VMBootParameters> m_boot_parameters;
		std::atomic_bool m_stop_requested{false};
	};
} // namespace AVPE
