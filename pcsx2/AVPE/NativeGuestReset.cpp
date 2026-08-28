// AVP:E native guest-reset control boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeGuestReset.h"

#include "AVPE/NativeAssets.h"
#include "AVPE/NativeAssetStateSnapshot.h"
#include "Host.h"
#include "VMManager.h"

#include <lucent/http.h>

#include <string>

namespace AVPE::NativeGuestReset
{
	lucent::http::Response Handle()
	{
		bool reset = false;
		bool was_running = false;
		std::string before;
		std::string after;
		NativeAssets::CacheSnapshot cache;
		Host::RunOnCPUThread([&]() {
			if (!VMManager::HasValidVM())
				return;
			was_running = VMManager::GetState() == VMState::Running;
			before = NativeAssetStateSnapshot::CaptureJsonOnCPUThread();
			if (was_running)
				VMManager::SetPaused(true);
			const u64 epoch_before = NativeAssets::GetGuestResetEpoch();
			VMManager::Reset();
			reset = NativeAssets::GetGuestResetEpoch() > epoch_before;
			after = NativeAssetStateSnapshot::CaptureJsonOnCPUThread();
			cache = NativeAssets::GetCacheSnapshot();
			if (was_running)
				VMManager::SetPaused(false);
		},
			true);
		if (!reset)
			return lucent::http::Response::json(409, "Conflict", "{\"reset\":false}");

		std::string cache_json = "{\"hits\":" + std::to_string(cache.hits);
		cache_json += ",\"misses\":" + std::to_string(cache.misses);
		cache_json += ",\"fills\":" + std::to_string(cache.fills);
		cache_json += ",\"evictions\":" + std::to_string(cache.evictions);
		cache_json += ",\"resident_pages\":" + std::to_string(cache.resident_pages);
		cache_json += ",\"resident_bytes\":" + std::to_string(cache.resident_bytes);
		cache_json += ",\"transient_handles\":" + std::to_string(cache.transient_handles);
		cache_json += ",\"peak_transient_handles\":" + std::to_string(cache.peak_transient_handles) + '}';
		return lucent::http::Response::json(200, "OK",
			"{\"reset\":true,\"was_running\":" + std::string(was_running ? "true" : "false") +
				",\"before\":" + before + ",\"after\":" + after +
				",\"cache\":" + cache_json + '}');
	}
} // namespace AVPE::NativeGuestReset
