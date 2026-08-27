// AVP:E native/optical byte-equivalence evidence. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Types.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace AVPE::NativeAssetByteTrace
{
	bool IsEnabled();
	void RegisterOpticalFile(std::string_view guest_path);
	void RecordNativeIomanBytes(std::string_view guest_path, u64 offset, const void* bytes, size_t size);
	void RecordNativeCdvdBytes(std::string_view guest_path, u64 offset, const void* bytes, size_t size);
	bool CaptureIsoOracle();
	std::string SnapshotJson();
	void Reset();
} // namespace AVPE::NativeAssetByteTrace
