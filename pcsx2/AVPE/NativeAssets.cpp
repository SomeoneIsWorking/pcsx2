// AVP:E native asset-I/O boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssets.h"

#include "AVPE/AVPE.h"
#include "AVPE/NativeAssetByteTrace.h"
#include "AVPE/NativeCdvdCompletion.h"
#include "AVPE/NativeAssetStore.h"
#include "AVPE/NativeLoadTiming.h"
#include "VMManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
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
		constexpr std::string_view kManifestSha256Environment = "AVPE_NATIVE_ASSET_MANIFEST_SHA256";
		constexpr u32 kNativeCdvdLsnBegin = 0xe0000000;
		constexpr u32 kNativeCdvdLsnEnd = 0xf0000000;
		constexpr u32 kCdvdSectorSize = 2048;
		constexpr u32 kMaximumCdvdReadSectors = 32;

		std::mutex s_observation_mutex;
		NativeAssetStore s_store;
		NativeAssetCache s_cache;
		u64 s_total_open_calls = 0;
		u64 s_dropped_unique_paths = 0;
		std::vector<OpenObservation> s_observed_paths;
		std::mutex s_cdvd_mutex;
		struct CdvdAsset
		{
			std::string guest_path;
			NativeAssetStoreRecord record;
			u32 base_lsn = 0;
			u32 sectors = 0;
		};
		std::vector<CdvdAsset> s_cdvd_assets;
		u32 s_next_cdvd_lsn = kNativeCdvdLsnBegin;
		struct ParsedPath
		{
			bool supported_namespace = false;
			bool stream_namespace = false;
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

		void ObserveOpen(const std::string_view path, const u32 flags)
		{
			if (!IsSurfacelessControlTest() || !IsTargetRecognized())
				return;
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

		std::string CdvdGuestPath(const std::string_view path)
		{
			std::string guest_path = "cdrom0:";
			guest_path.append(path);
			std::replace(guest_path.begin(), guest_path.end(), '\\', '/');
			return guest_path;
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
			const bool stream_namespace = relative.starts_with("STREAMS/");
			const bool supported_namespace =
				relative.starts_with("TBD/") || relative.starts_with("MOVIES/") || stream_namespace;
			if (!supported_namespace)
				return {};
			if (relative.ends_with(";1"))
				relative.resize(relative.size() - 2);
			else if (relative.find(';') != std::string::npos)
				return {.supported_namespace = true, .stream_namespace = stream_namespace};
			if (relative.empty() || relative.front() == '/' || relative.back() == '/')
				return {.supported_namespace = true, .stream_namespace = stream_namespace};

			size_t component_start = 0;
			for (size_t index = 0; index <= relative.size(); ++index)
			{
				if (index != relative.size() && relative[index] != '/')
					continue;
				const std::string_view component(relative.data() + component_start, index - component_start);
				if (component.empty() || component == "." || component == ".." || component.find(':') != std::string_view::npos)
					return {.supported_namespace = true, .stream_namespace = stream_namespace};
				component_start = index + 1;
			}
			return {
				.supported_namespace = true,
				.stream_namespace = stream_namespace,
				.relative = std::move(relative),
			};
		}

		void NoteRefusal(const std::string_view path)
		{
			std::lock_guard lock(s_observation_mutex);
			if (OpenObservation* observation = FindObservation(path))
				++observation->refused_count;
		}

		OpenResolution ResolveStoreFile(const std::string_view path, const bool streams_only)
		{
			const ParsedPath parsed = ParseSupportedPath(path);
			const char* const configured_root = std::getenv(kStoreEnvironment.data());
			if (!IsTargetRecognized() || !parsed.supported_namespace || !configured_root || !*configured_root)
				return {};
			if (streams_only && !parsed.stream_namespace)
				return {};
			if (!parsed.relative)
				return {.disposition = OpenDisposition::RefusedAccess};
			if (streams_only && !parsed.relative->ends_with(".VAG") && !parsed.relative->ends_with(".ZIV"))
				return {};

			const char* const manifest_sha256 = std::getenv(kManifestSha256Environment.data());
			if (!manifest_sha256 || !*manifest_sha256)
				return {.disposition = OpenDisposition::RefusedInvalidStore};

			const NativeAssetStoreResult result = s_store.Resolve(configured_root, manifest_sha256, *parsed.relative);
			if (result.disposition == NativeAssetStoreDisposition::Missing)
				return {.disposition = OpenDisposition::RefusedMissing};
			if (result.disposition != NativeAssetStoreDisposition::Found)
				return {.disposition = OpenDisposition::RefusedInvalidStore};
			return {
				.disposition = OpenDisposition::NativeFile,
				.record = result.record,
			};
		}

		std::optional<CdvdAsset> FindCdvdAsset(const u32 lsn, const u32 sectors)
		{
			std::lock_guard lock(s_cdvd_mutex);
			const u64 request_end = static_cast<u64>(lsn) + sectors;
			const auto asset = std::find_if(s_cdvd_assets.begin(), s_cdvd_assets.end(),
				[lsn, request_end](const CdvdAsset& candidate) {
					return lsn >= candidate.base_lsn && lsn < static_cast<u64>(candidate.base_lsn) + candidate.sectors &&
				           request_end <= static_cast<u64>(candidate.base_lsn) + candidate.sectors;
				});
			return asset == s_cdvd_assets.end() ? std::nullopt : std::optional<CdvdAsset>(*asset);
		}
	} // namespace

	OpenResolution ResolveIomanOpen(const std::string_view path, const u32 flags, const bool read_only)
	{
		ObserveOpen(path, flags);

		const ParsedPath parsed = ParseSupportedPath(path);
		if (!IsTargetRecognized() || !parsed.supported_namespace)
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
		if (*parsed.relative == "TBD/TBF.TBF")
			NativeLoadTiming::NoteTbfOpen();
		NativeAssetByteTrace::RegisterOpticalFile(path);

		OpenResolution resolution = ResolveStoreFile(path, false);
		if (*parsed.relative == "TBD/TBF.TBF")
		{
			NativeLoadTiming::Backend backend = NativeLoadTiming::Backend::Refused;
			if (resolution.disposition == OpenDisposition::NativeFile)
				backend = NativeLoadTiming::Backend::Native;
			else if (resolution.disposition == OpenDisposition::Unhandled)
				backend = NativeLoadTiming::Backend::Optical;
			NativeLoadTiming::NoteTbfBackend(backend);
		}
		if (resolution.disposition != OpenDisposition::NativeFile && resolution.disposition != OpenDisposition::Unhandled)
			NoteRefusal(path);
		return resolution;
	}

	CdvdSearchResolution ResolveCdvdSearch(const std::string_view path)
	{
		const std::string guest_path = CdvdGuestPath(path);
		ObserveOpen(guest_path, 1);
		const ParsedPath parsed = ParseSupportedPath(guest_path);
		NativeLoadTiming::NoteCdvdSearch(parsed.relative && *parsed.relative == "STREAMS/MENU01.ZIV");
		if (parsed.relative)
			NativeAssetByteTrace::RegisterOpticalFile(guest_path);
		const OpenResolution file = ResolveStoreFile(guest_path, true);
		if (file.disposition != OpenDisposition::NativeFile)
		{
			if (file.disposition != OpenDisposition::Unhandled)
				NoteRefusal(guest_path);
			return {.disposition = file.disposition};
		}

		const u64 size = file.record.size;
		if (size == 0 || size > std::numeric_limits<u32>::max())
		{
			NoteRefusal(guest_path);
			return {.disposition = OpenDisposition::RefusedInvalidStore};
		}
		const u32 sectors = static_cast<u32>((size + kCdvdSectorSize - 1) / kCdvdSectorSize);
		CdvdSearchResolution resolution;
		{
			std::lock_guard lock(s_cdvd_mutex);
			const auto existing = std::find_if(s_cdvd_assets.begin(), s_cdvd_assets.end(),
				[&guest_path](const CdvdAsset& asset) { return asset.guest_path == guest_path; });
			if (existing != s_cdvd_assets.end())
			{
				resolution = {
					.disposition = OpenDisposition::NativeFile,
					.lsn = existing->base_lsn,
					.size = static_cast<u32>(existing->record.size),
				};
			}
			else
			{
				if (static_cast<u64>(s_next_cdvd_lsn) + sectors > kNativeCdvdLsnEnd)
				{
					NoteRefusal(guest_path);
					return {.disposition = OpenDisposition::RefusedInvalidStore};
				}
				const u32 base_lsn = s_next_cdvd_lsn;
				s_next_cdvd_lsn += sectors;
				s_cdvd_assets.push_back({
					.guest_path = guest_path,
					.record = file.record,
					.base_lsn = base_lsn,
					.sectors = sectors,
				});
				resolution = {
					.disposition = OpenDisposition::NativeFile,
					.lsn = base_lsn,
					.size = static_cast<u32>(size),
				};
			}
		}
		NoteNativeOpen(guest_path);
		return resolution;
	}

	CdvdDisposition ResolveCdvdSeek(const u32 lsn)
	{
		const std::optional<CdvdAsset> asset = FindCdvdAsset(lsn, 0);
		if (!asset)
			return lsn >= kNativeCdvdLsnBegin && lsn < kNativeCdvdLsnEnd ? CdvdDisposition::Failed :
			                                                               CdvdDisposition::Unhandled;
		NoteNativeSeek(asset->guest_path);
		return CdvdDisposition::Complete;
	}

	CdvdReadResolution ReadCdvdSectors(const u32 lsn, const u32 sectors)
	{
		if (sectors == 0 || sectors > kMaximumCdvdReadSectors)
			return {.disposition = lsn >= kNativeCdvdLsnBegin && lsn < kNativeCdvdLsnEnd ?
			                           CdvdDisposition::Failed :
			                           CdvdDisposition::Unhandled};
		const std::optional<CdvdAsset> asset = FindCdvdAsset(lsn, sectors);
		if (!asset)
			return {.disposition = lsn >= kNativeCdvdLsnBegin && lsn < kNativeCdvdLsnEnd ?
			                           CdvdDisposition::Failed :
			                           CdvdDisposition::Unhandled};

		const size_t byte_count = static_cast<size_t>(sectors) * kCdvdSectorSize;
		std::vector<u8> bytes(byte_count, 0);
		const u64 offset = static_cast<u64>(lsn - asset->base_lsn) * kCdvdSectorSize;
		const size_t available = offset >= asset->record.size ?
		                             0 :
		                             static_cast<size_t>(std::min<u64>(byte_count, asset->record.size - offset));
		const ReadResult read = Read(asset->record, offset, std::span<u8>(bytes.data(), available));
		if (available != 0 &&
			(read.disposition != ReadDisposition::Complete || read.bytes_read != available))
		{
			NoteNativeRead(asset->guest_path, static_cast<u32>(byte_count), -1);
			return {.disposition = CdvdDisposition::Failed};
		}
		NativeAssetByteTrace::RecordNativeCdvdBytes(asset->guest_path, offset, bytes.data(), bytes.size());
		NoteNativeRead(asset->guest_path, static_cast<u32>(byte_count), static_cast<s32>(byte_count));
		return {.disposition = CdvdDisposition::Complete, .bytes = std::move(bytes)};
	}

	ReadResult Read(const NativeAssetStoreRecord& record, const u64 offset, const std::span<u8> destination)
	{
		const NativeAssetStoreResult validation = s_store.Validate(record);
		if (validation.disposition != NativeAssetStoreDisposition::Found)
		{
			s_cache.Unbind();
			return {.error = validation.error};
		}
		return s_cache.ReadAt(validation.record, offset, destination);
	}

	OpenResolution ResolveSavedFile(const std::string_view path)
	{
		const ParsedPath parsed = ParseSupportedPath(path);
		if (!IsTargetRecognized() || !parsed.supported_namespace || !parsed.relative)
			return {.disposition = OpenDisposition::RefusedInvalidStore};
		return ResolveStoreFile(path, false);
	}

	std::vector<CdvdMappingState> GetCdvdMappingState()
	{
		std::lock_guard lock(s_cdvd_mutex);
		std::vector<CdvdMappingState> result;
		result.reserve(s_cdvd_assets.size());
		for (const CdvdAsset& asset : s_cdvd_assets)
		{
			result.push_back({
				.guest_path = asset.guest_path,
				.base_lsn = asset.base_lsn,
				.size = asset.record.size,
				.sha256 = asset.record.sha256,
			});
		}
		return result;
	}

	bool RestoreCdvdMappingState(const std::vector<CdvdMappingState>& mappings, const u32 next_lsn)
	{
		std::vector<CdvdAsset> restored;
		restored.reserve(mappings.size());
		u32 minimum_next_lsn = kNativeCdvdLsnBegin;
		for (const CdvdMappingState& mapping : mappings)
		{
			const OpenResolution resolution = ResolveSavedFile(mapping.guest_path);
			if (resolution.disposition != OpenDisposition::NativeFile || resolution.record.size != mapping.size ||
				resolution.record.sha256 != mapping.sha256 || mapping.size == 0 ||
				mapping.size > std::numeric_limits<u32>::max())
			{
				return false;
			}
			const u32 sectors = static_cast<u32>((mapping.size + kCdvdSectorSize - 1) / kCdvdSectorSize);
			const u64 mapping_end = static_cast<u64>(mapping.base_lsn) + sectors;
			if (mapping.base_lsn < kNativeCdvdLsnBegin || mapping_end > kNativeCdvdLsnEnd)
				return false;
			const bool overlaps = std::any_of(restored.begin(), restored.end(), [&mapping, mapping_end](const CdvdAsset& asset) {
				const u64 asset_end = static_cast<u64>(asset.base_lsn) + asset.sectors;
				return mapping.base_lsn < asset_end && asset.base_lsn < mapping_end;
			});
			if (overlaps)
				return false;
			restored.push_back({
				.guest_path = mapping.guest_path,
				.record = resolution.record,
				.base_lsn = mapping.base_lsn,
				.sectors = sectors,
			});
			minimum_next_lsn = std::max(minimum_next_lsn, static_cast<u32>(mapping_end));
		}
		if (next_lsn < minimum_next_lsn || next_lsn > kNativeCdvdLsnEnd)
			return false;

		std::lock_guard lock(s_cdvd_mutex);
		s_cdvd_assets = std::move(restored);
		s_next_cdvd_lsn = next_lsn;
		return true;
	}

	u32 GetNextCdvdLsn()
	{
		std::lock_guard lock(s_cdvd_mutex);
		return s_next_cdvd_lsn;
	}

	CacheSnapshot GetCacheSnapshot()
	{
		return s_cache.Snapshot();
	}

	void DropCache()
	{
		s_cache.DropPages();
	}

	void ResetGuestState()
	{
		NativeCdvdCompletion::Reset();
		std::lock_guard lock(s_cdvd_mutex);
		s_cdvd_assets.clear();
		s_next_cdvd_lsn = kNativeCdvdLsnBegin;
	}

	void UnbindStore()
	{
		ResetGuestState();
		s_cache.Unbind();
		s_store.Unbind();
	}

	void NoteOriginalFallback(const std::string_view path)
	{
		if (!IsSurfacelessControlTest() || !IsTargetRecognized())
			return;
		std::lock_guard lock(s_observation_mutex);
		if (OpenObservation* observation = FindObservation(path))
			++observation->original_fallback_count;
	}

	void NoteCdvdOriginalFallback(const std::string_view path)
	{
		NoteOriginalFallback(CdvdGuestPath(path));
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
		{
			std::lock_guard lock(s_observation_mutex);
			s_total_open_calls = 0;
			s_dropped_unique_paths = 0;
			s_observed_paths.clear();
		}
	}
} // namespace AVPE::NativeAssets
