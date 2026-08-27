// AVPE-owned persistence setup for the PCSX2 core.
#include "pcsx2-avpe/Settings.h"

#include "pcsx2/Config.h"
#include "pcsx2/Host.h"
#include "pcsx2/INISettingsInterface.h"
#include "pcsx2/VMManager.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include <lucent/log.h>

#include <memory>

namespace AVPE::Settings
{
	static std::unique_ptr<INISettingsInterface> s_base;
	static std::unique_ptr<INISettingsInterface> s_secrets;

	bool Initialize(const std::string& data_path)
	{
		Error error;
		if (!data_path.empty() && !FileSystem::EnsureDirectoryExists(data_path.c_str(), true, &error))
		{
			lucent::error("avpe-settings", "could not create data root: {}", error.GetDescription());
			return false;
		}
		EmuConfig.CustomDataPath = data_path;
		EmuFolders::SetAppRoot();
		if (!EmuFolders::SetResourcesDirectory())
		{
			lucent::error("avpe-settings", "PCSX2 core resources are missing beside the AVPE executable");
			return false;
		}

		if (!EmuFolders::SetDataDirectory(&error))
		{
			lucent::error("avpe-settings", "could not create data directory: {}", error.GetDescription());
			return false;
		}

		const std::string base_path = Path::Combine(EmuFolders::Settings, "PCSX2.ini");
		const bool base_exists = FileSystem::FileExists(base_path.c_str());
		s_base = std::make_unique<INISettingsInterface>(base_path);
		Host::Internal::SetBaseSettingsLayer(s_base.get());
		if (!base_exists || !s_base->Load() || !VMManager::Internal::CheckSettingsVersion())
		{
			VMManager::SetDefaultSettings(*s_base, true, true, true, true, true);
			if (!s_base->Save(&error))
			{
				lucent::error("avpe-settings", "could not save settings: {}", error.GetDescription());
				return false;
			}
		}

		const std::string secrets_path = Path::Combine(EmuFolders::Settings, "secrets.ini");
		s_secrets = std::make_unique<INISettingsInterface>(secrets_path);
		Host::Internal::SetSecretsSettingsLayer(s_secrets.get());
		if (!FileSystem::FileExists(secrets_path.c_str()) || !s_secrets->Load())
		{
			if (!s_secrets->Save(&error))
			{
				lucent::error("avpe-settings", "could not save secrets: {}", error.GetDescription());
				return false;
			}
		}

		VMManager::Internal::LoadStartupSettings();
		return true;
	}

	void Save()
	{
		if (!s_base || !s_base->IsDirty())
			return;
		Error error;
		if (!s_base->Save(&error))
			lucent::error("avpe-settings", "could not save settings: {}", error.GetDescription());
	}

	void Shutdown()
	{
		Save();
	}
} // namespace AVPE::Settings
