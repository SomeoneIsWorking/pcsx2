// AVP:E native asset-I/O boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssets.h"

#include "AVPE/AVPE.h"
#include "VMManager.h"

#include <algorithm>
#include <mutex>
#include <utility>

namespace AVPE::NativeAssets
{
	namespace
	{
		constexpr std::string_view kTargetSerial = "SLUS-20147";
		constexpr size_t kMaximumObservedPaths = 128;

		std::mutex s_observation_mutex;
		u64 s_total_open_calls = 0;
		u64 s_dropped_unique_paths = 0;
		std::vector<OpenObservation> s_observed_paths;

		bool IsTargetRecognized()
		{
			return VMManager::GetDiscSerial() == kTargetSerial;
		}
	} // namespace

	void ObserveIomanOpen(const std::string_view path, const u32 flags)
	{
		if (!IsSurfacelessControlTest() || !IsTargetRecognized())
			return;

		std::lock_guard lock(s_observation_mutex);
		++s_total_open_calls;
		const auto existing = std::find_if(s_observed_paths.begin(), s_observed_paths.end(),
			[path](const OpenObservation& observation) { return observation.path == path; });
		if (existing != s_observed_paths.end())
		{
			++existing->count;
			return;
		}

		if (s_observed_paths.size() == kMaximumObservedPaths)
		{
			++s_dropped_unique_paths;
			return;
		}

		OpenObservation observation;
		observation.path = path;
		observation.flags = flags;
		observation.count = 1;
		s_observed_paths.push_back(std::move(observation));
	}

	ObservationSnapshot GetObservationSnapshot()
	{
		ObservationSnapshot snapshot;
		snapshot.enabled = IsSurfacelessControlTest();
		snapshot.target_recognized = IsTargetRecognized();
		std::lock_guard lock(s_observation_mutex);
		snapshot.total_open_calls = s_total_open_calls;
		snapshot.dropped_unique_paths = s_dropped_unique_paths;
		snapshot.paths = s_observed_paths;
		return snapshot;
	}

	void ResetObservation()
	{
		std::lock_guard lock(s_observation_mutex);
		s_total_open_calls = 0;
		s_dropped_unique_paths = 0;
		s_observed_paths.clear();
	}
} // namespace AVPE::NativeAssets
