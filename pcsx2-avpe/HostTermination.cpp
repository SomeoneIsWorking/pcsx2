// AVPE product-host termination signals. Fork-local; not for upstream PCSX2.
#include "pcsx2-avpe/HostTermination.h"

#include <lucent/log.h>

#include <QtCore/QSocketNotifier>

#include <cstdlib>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace AVPE
{
#ifndef _WIN32
	static constexpr int TerminationSignals[] = {SIGTERM, SIGINT, SIGHUP};

	HostTermination::HostTermination(std::function<void()> request_exit)
		: m_request_exit(std::move(request_exit))
	{
		int fds[2] = {-1, -1};
		if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
		{
			lucent::error("avpe", "termination signal pipe could not be created");
			std::abort();
		}
		m_read_fd = fds[0];
		s_write_fd = fds[1];
		m_notifier = std::make_unique<QSocketNotifier>(m_read_fd, QSocketNotifier::Read);
		connect(m_notifier.get(), &QSocketNotifier::activated, this, [this]() { Drain(); });

		struct sigaction action = {};
		action.sa_handler = &HostTermination::OnSignal;
		sigemptyset(&action.sa_mask);
		action.sa_flags = SA_RESTART | SA_RESETHAND;
		for (const int signal : TerminationSignals)
		{
			sigaction(signal, &action, nullptr);
		}
	}

	HostTermination::~HostTermination()
	{
		for (const int signal : TerminationSignals)
		{
			std::signal(signal, SIG_DFL);
		}
		m_notifier.reset();
		if (s_write_fd >= 0)
		{
			close(s_write_fd);
			s_write_fd = -1;
		}
		if (m_read_fd >= 0)
		{
			close(m_read_fd);
		}
	}

	void HostTermination::OnSignal(const int signal)
	{
		const char byte = static_cast<char>(signal);
		[[maybe_unused]] const ssize_t written = write(s_write_fd, &byte, 1);
	}

	void HostTermination::Drain()
	{
		char byte = 0;
		while (read(m_read_fd, &byte, 1) == 1)
		{
		}
		lucent::info("avpe", "termination signal {}; shutting down", static_cast<int>(byte));
		m_request_exit();
	}
#else
	// A Windows GUI process receives no POSIX termination signals.
	HostTermination::HostTermination(std::function<void()> request_exit)
		: m_request_exit(std::move(request_exit))
	{
	}

	HostTermination::~HostTermination() = default;

	void HostTermination::OnSignal(int)
	{
	}

	void HostTermination::Drain()
	{
	}
#endif
} // namespace AVPE
