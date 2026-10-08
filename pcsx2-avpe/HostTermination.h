// AVPE product-host termination signals. Fork-local; not for upstream PCSX2.
#pragma once

#include <QtCore/QObject>

#include <functional>
#include <memory>

class QSocketNotifier;

namespace AVPE
{
	// Turns SIGTERM, SIGINT and SIGHUP into one graceful exit request on the Qt
	// thread; a second signal takes the default action. Installed before SDL, which
	// would otherwise claim them as an SDL quit event nothing reads.
	class HostTermination final : public QObject
	{
	public:
		explicit HostTermination(std::function<void()> request_exit);
		~HostTermination() override;

		HostTermination(const HostTermination&) = delete;
		HostTermination& operator=(const HostTermination&) = delete;

	private:
		static void OnSignal(int signal);
		void Drain();

		// Written only by the signal handler, which cannot reach an instance.
		static inline int s_write_fd = -1;

		std::function<void()> m_request_exit;
		int m_read_fd = -1;
		std::unique_ptr<QSocketNotifier> m_notifier;
	};
} // namespace AVPE
