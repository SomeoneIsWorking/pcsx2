// AVP:E native/optical loading-boundary evidence. Fork-local; not for upstream PCSX2.

#pragma once

#include <cstdint>
#include <string>

namespace AVPE::NativeLoadTiming
{
	enum class Backend : std::uint8_t
	{
		Optical,
		Native,
		Refused,
	};

	void NoteTbfOpen();
	void NoteTbfBackend(Backend backend);
	void NoteCdvdSearch(bool menu01);
	void NoteCdvdSeek();
	void NoteCdvdSeekBackend(Backend backend);
	std::string SnapshotJson();
	void Reset();
} // namespace AVPE::NativeLoadTiming
