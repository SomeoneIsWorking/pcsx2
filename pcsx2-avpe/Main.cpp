// Standalone AVPE frontend. PCSX2 is linked as an emulation-core library.
#include "pcsx2-avpe/Runtime.h"
#include "pcsx2-avpe/Settings.h"

#include "pcsx2/Config.h"
#include "pcsx2/VMManager.h"

#include "common/CrashHandler.h"

#include <lucent/log.h>

#include <QtGui/QGuiApplication>
#include <QtWidgets/QApplication>

#include <cstdlib>
#include <locale>
#include <optional>
#include <string>

namespace
{
	struct CommandLine
	{
		VMBootParameters boot;
		std::string data_path;
		std::string log_path;
		bool test_config = false;
	};

	void PrintUsage(const char* program)
	{
		std::fprintf(stderr,
			"Usage: %s [-datapath PATH] [-logfile PATH] [-fastboot|-slowboot] "
			"[-fullscreen|-nofullscreen] [--test-config] [--] GAME\n",
			program);
	}

	std::optional<CommandLine> ParseCommandLine(const int argc, char** argv)
	{
		CommandLine command;
		bool options = true;
		for (int index = 1; index < argc; ++index)
		{
			const std::string argument(argv[index]);
			if (options && argument == "--")
			{
				options = false;
				continue;
			}
			if (options && argument == "-datapath" && index + 1 < argc)
				command.data_path = argv[++index];
			else if (options && argument == "-logfile" && index + 1 < argc)
				command.log_path = argv[++index];
			else if (options && argument == "-fastboot")
				command.boot.fast_boot = true;
			else if (options && argument == "-slowboot")
				command.boot.fast_boot = false;
			else if (options && argument == "-fullscreen")
				command.boot.fullscreen = true;
			else if (options && argument == "-nofullscreen")
				command.boot.fullscreen = false;
			else if (options && argument == "--test-config")
				command.test_config = true;
			else if (options && (argument == "-help" || argument == "--help"))
			{
				PrintUsage(argv[0]);
				return std::nullopt;
			}
			else if (options && !argument.empty() && argument.front() == '-')
			{
				lucent::error("avpe", "unknown option: {}", argument);
				return std::nullopt;
			}
			else if (command.boot.filename.empty())
				command.boot.filename = argument;
			else
			{
				lucent::error("avpe", "multiple game paths were supplied");
				return std::nullopt;
			}
		}
		return command;
	}
} // namespace

int main(int argc, char* argv[])
{
	CrashHandler::Install();
#ifdef _WIN32
	std::locale::global(std::locale(""));
#endif
	QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
		Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
	QApplication application(argc, argv);
	application.setApplicationName(QStringLiteral("AVP: Extinction"));

	const std::optional<CommandLine> command = ParseCommandLine(argc, argv);
	if (!command.has_value())
		return EXIT_FAILURE;
	if (!AVPE::Settings::Initialize(command->data_path))
		return EXIT_FAILURE;
	if (!command->log_path.empty())
		VMManager::Internal::SetFileLogPath(command->log_path);
	if (command->test_config)
	{
		AVPE::Settings::Shutdown();
		return EXIT_SUCCESS;
	}
	if (command->boot.filename.empty())
	{
		lucent::error("avpe", "no game path was supplied");
		AVPE::Settings::Shutdown();
		return EXIT_FAILURE;
	}

	const char* hardware_error = nullptr;
	if (!VMManager::PerformEarlyHardwareChecks(&hardware_error))
	{
		lucent::error("avpe", "host CPU is unsupported: {}", hardware_error ? hardware_error : "unknown reason");
		AVPE::Settings::Shutdown();
		return EXIT_FAILURE;
	}

	int result;
	{
		AVPE::Runtime runtime(application);
		runtime.Start(command->boot);
		result = application.exec();
		runtime.Stop();
	}
	AVPE::Settings::Shutdown();
	return result;
}
