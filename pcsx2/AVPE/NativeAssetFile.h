// AVP:E cache-backed ioman file adapter. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/NativeAssetStore.h"

class IOManFile;

namespace AVPE::NativeAssetFile
{
	int Open(IOManFile** file, const NativeAssetStoreRecord& record);
} // namespace AVPE::NativeAssetFile
