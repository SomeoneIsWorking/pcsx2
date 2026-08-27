// AVP:E native asset-I/O boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssets.h"

#include "AVPE/AVPE.h"
#include "VMManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <utility>

namespace AVPE::NativeAssets
{
	namespace
	{
		constexpr std::string_view kTargetSerial = "SLUS-20147";
		constexpr size_t kMaximumObservedPaths = 128;
		constexpr std::string_view kStoreEnvironment = "AVPE_NATIVE_ASSET_ROOT";

		std::mutex s_observation_mutex;
		u64 s_total_open_calls = 0;
		u64 s_dropped_unique_paths = 0;
		std::vector<OpenObservation> s_observed_paths;
		struct ParsedPath
		{
			bool supported_namespace = false;
			std::optional<std::string> relative;
		};

		bool IsTargetRecognized()
		{
			return VMManager::GetDiscSerial() == kTargetSerial;
		}

		OpenObservation* FindObservation(const std::string_view path)
		{
			const auto existing = std::find_if(s_observed_paths.begin(), s_observed_paths.end(),
				[path](const OpenObservation& observation) { return observation.path == path; });
			return existing == s_observed_paths.end() ? nullptr : &*existing;
		}

		ParsedPath ParseSupportedPath(const std::string_view path)
		{
			constexpr std::string_view device = "cdrom0:";
			if (path.size() <= device.size())
				return {};
			for (size_t index = 0; index < device.size(); ++index)
			{
				if (std::tolower(static_cast<unsigned char>(path[index])) != device[index])
					return {};
			}

			std::string relative(path.substr(device.size()));
			std::replace(relative.begin(), relative.end(), '\\', '/');
			while (!relative.empty() && relative.front() == '/')
				relative.erase(relative.begin());
			std::transform(relative.begin(), relative.end(), relative.begin(),
				[](const unsigned char character) { return static_cast<char>(std::toupper(character)); });
			const bool supported_namespace = relative.starts_with("TBD/") || relative.starts_with("MOVIES/") ||
			                                 relative.starts_with("STREAMS/");
			if (!supported_namespace)
				return {};
			if (relative.ends_with(";1"))
				relative.resize(relative.size() - 2);
			else if (relative.find(';') != std::string::npos)
				return {.supported_namespace = true};
			if (relative.empty() || relative.front() == '/' || relative.back() == '/')
				return {.supported_namespace = true};

			size_t component_start = 0;
			for (size_t index = 0; index <= relative.size(); ++index)
			{
				if (index != relative.size() && relative[index] != '/')
					continue;
				const std::string_view component(relative.data() + component_start, index - component_start);
				if (component.empty() || component == "." || component == ".." || component.find(':') != std::string_view::npos)
					return {.supported_namespace = true};
				component_start = index + 1;
			}
			return {
				.supported_namespace = true,
				.relative = std::move(relative),
			};
		}

		bool IsDescendant(const std::filesystem::path& root, const std::filesystem::path& candidate)
		{
			auto root_component = root.begin();
			auto candidate_component = candidate.begin();
			for (; root_component != root.end(); ++root_component, ++candidate_component)
			{
				if (candidate_component == candidate.end() || *root_component != *candidate_component)
					return false;
			}
			return candidate_component != candidate.end();
		}

		void NoteRefusal(const std::string_view path)
		{
			std::lock_guard lock(s_observation_mutex);
			if (OpenObservation* observation = FindObservation(path))
				++observation->refused_count;
		}
	} // namespace

	OpenResolution ResolveIomanOpen(const std::string_view path, const u32 flags, const bool read_only)
	{
		const bool target_recognized = IsTargetRecognized();
		if (IsSurfacelessControlTest() && target_recognized)
		{
			std::lock_guard lock(s_observation_mutex);
			++s_total_open_calls;
			if (OpenObservation* existing = FindObservation(path))
			{
				++existing->count;
			}
			else if (s_observed_paths.size() == kMaximumObservedPaths)
			{
				++s_dropped_unique_paths;
			}
			else
			{
				OpenObservation observation;
				observation.path = path;
				observation.flags = flags;
				observation.count = 1;
				s_observed_paths.push_back(std::move(observation));
			}
		}

		const ParsedPath parsed = ParseSupportedPath(path);
		const char* const configured_root = std::getenv(kStoreEnvironment.data());
		if (!target_recognized || !parsed.supported_namespace || !configured_root || !*configured_root)
			return {};
		if (!parsed.relative)
		{
			NoteRefusal(path);
			return {.disposition = OpenDisposition::RefusedAccess};
		}
		if (!read_only)
		{
			NoteRefusal(path);
			return {.disposition = OpenDisposition::RefusedAccess};
		}

		std::error_code error;
		const std::filesystem::path root = std::filesystem::canonical(configured_root, error);
		if (error || !std::filesystem::is_directory(root, error) || error ||
			!std::filesystem::is_regular_file(root.parent_path() / "manifest.json", error) || error)
		{
			NoteRefusal(path);
			return {.disposition = OpenDisposition::RefusedInvalidStore};
		}
		const std::filesystem::path candidate = std::filesystem::canonical(root / *parsed.relative, error);
		if (error || !std::filesystem::is_regular_file(candidate, error) || error)
		{
			NoteRefusal(path);
			return {.disposition = OpenDisposition::RefusedMissing};
		}
		if (!IsDescendant(root, candidate))
		{
			NoteRefusal(path);
			return {.disposition = OpenDisposition::RefusedInvalidStore};
		}
		return {
			.disposition = OpenDisposition::NativeFile,
			.host_path = candidate.string(),
		};
	}

	void NoteNativeOpen(const std::string_view path)
	{
		std::lock_guard lock(s_observation_mutex);
		if (OpenObservation* observation = FindObservation(path))
			++observation->native_open_count;
	}

	void NoteNativeRead(const std::string_view path, const u32 bytes_requested, const s32 result)
	{
		std::lock_guard lock(s_observation_mutex);
		if (OpenObservation* observation = FindObservation(path))
		{
			++observation->read_calls;
			if (result > 0)
				observation->bytes_read += std::min<u64>(static_cast<u32>(result), bytes_requested);
		}
	}

	void NoteNativeSeek(const std::string_view path)
	{
		std::lock_guard lock(s_observation_mutex);
		if (OpenObservation* observation = FindObservation(path))
			++observation->seek_calls;
	}

	void NoteNativeClose(const std::string_view path)
	{
		std::lock_guard lock(s_observation_mutex);
		if (OpenObservation* observation = FindObservation(path))
			++observation->close_count;
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
