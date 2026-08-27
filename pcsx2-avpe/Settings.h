// AVPE-owned persistence setup for the PCSX2 core.
#pragma once

#include <string>

namespace AVPE::Settings
{
	bool Initialize(const std::string& data_path);
	void Shutdown();
	void Save();
} // namespace AVPE::Settings
