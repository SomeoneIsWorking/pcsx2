// AVP:E native asset-I/O boundary. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/NativeAssetCache.h"
#include "AVPE/NativeAssetStore.h"
#include "common/Pcsx2Types.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace AVPE::NativeAssets
{
	enum class OpenDisposition : u8
	{
		Unhandled,
		NativeFile,
		RefusedMissing,
		RefusedAccess,
		RefusedInvalidStore,
	};

	struct OpenResolution
	{
		OpenDisposition disposition = OpenDisposition::Unhandled;
		NativeAssetStoreRecord record;
	};

	using ReadDisposition = NativeAssetCacheReadDisposition;
	using ReadResult = NativeAssetCacheReadResult;
	using CacheSnapshot = NativeAssetCacheSnapshot;

	enum class CdvdDisposition : u8
	{
		Unhandled,
		Complete,
		Failed,
	};

	struct CdvdSearchResolution
	{
		OpenDisposition disposition = OpenDisposition::Unhandled;
		u32 lsn = 0;
		u32 size = 0;
	};

	struct CdvdReadResolution
	{
		CdvdDisposition disposition = CdvdDisposition::Unhandled;
		std::vector<u8> bytes;
	};

	struct CdvdMappingState
	{
		std::string guest_path;
		u32 base_lsn = 0;
		u64 size = 0;
		std::string sha256;
	};

	struct OpenObservation
	{
		std::string path;
		u32 flags = 0;
		u32 count = 0;
		u32 native_open_count = 0;
		u32 original_fallback_count = 0;
		u32 refused_count = 0;
		u32 read_calls = 0;
		u64 bytes_read = 0;
		u32 seek_calls = 0;
		u32 close_count = 0;
	};

	struct ObservationSnapshot
	{
		bool enabled = false;
		bool target_recognized = false;
		u64 total_open_calls = 0;
		u64 dropped_unique_paths = 0;
		std::vector<OpenObservation> paths;
	};

	// Resolves only the title's grounded read-only namespaces. With no validated
	// store environment it observes and leaves the original IOP path untouched.
	OpenResolution ResolveIomanOpen(std::string_view path, u32 flags, bool read_only);
	CdvdSearchResolution ResolveCdvdSearch(std::string_view path);
	CdvdDisposition ResolveCdvdSeek(u32 lsn);
	CdvdReadResolution ReadCdvdSectors(u32 lsn, u32 sectors);
	ReadResult Read(const NativeAssetStoreRecord& record, u64 offset, std::span<u8> destination);
	OpenResolution ResolveSavedFile(std::string_view path);
	std::vector<CdvdMappingState> GetCdvdMappingState();
	bool RestoreCdvdMappingState(const std::vector<CdvdMappingState>& mappings, u32 next_lsn);
	u32 GetNextCdvdLsn();
	CacheSnapshot GetCacheSnapshot();
	void DropCache();
	void ResetGuestState();
	void UnbindStore();
	void NoteOriginalFallback(std::string_view path);
	void NoteCdvdOriginalFallback(std::string_view path);
	void NoteNativeOpen(std::string_view path);
	void NoteNativeRead(std::string_view path, u32 bytes_requested, s32 result);
	void NoteNativeSeek(std::string_view path);
	void NoteNativeClose(std::string_view path);
	ObservationSnapshot GetObservationSnapshot();
	void ResetObservation();
} // namespace AVPE::NativeAssets
