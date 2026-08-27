// AVP:E bounded native-asset byte cache. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/NativeAssetStore.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace AVPE
{
	enum class NativeAssetCacheReadDisposition : std::uint8_t
	{
		Complete,
		ShortRead,
		EndOfFile,
		IoError,
	};

	struct NativeAssetCacheReadResult
	{
		NativeAssetCacheReadDisposition disposition = NativeAssetCacheReadDisposition::IoError;
		std::size_t bytes_read = 0;
		std::string error;
	};

	struct NativeAssetCacheSnapshot
	{
		std::uint64_t hits = 0;
		std::uint64_t misses = 0;
		std::uint64_t fills = 0;
		std::uint64_t evictions = 0;
		std::size_t resident_pages = 0;
		std::size_t resident_bytes = 0;
		std::uint32_t transient_handles = 0;
		std::uint32_t peak_transient_handles = 0;
	};

	// Immutable 64 KiB pages with a strict 32 MiB/512-page LRU bound. The
	// admitted record remains the authority for path, identity, and logical EOF.
	class NativeAssetCache final
	{
	public:
		static constexpr std::size_t PageBytes = 64 * 1024;
		static constexpr std::size_t MaximumPages = 512;
		static constexpr std::size_t MaximumResidentBytes = PageBytes * MaximumPages;

		NativeAssetCache();
		~NativeAssetCache();

		NativeAssetCache(const NativeAssetCache&) = delete;
		NativeAssetCache& operator=(const NativeAssetCache&) = delete;
		NativeAssetCache(NativeAssetCache&&) = delete;
		NativeAssetCache& operator=(NativeAssetCache&&) = delete;

		NativeAssetCacheReadResult ReadAt(
			const NativeAssetStoreRecord& record, std::uint64_t offset, std::span<std::uint8_t> destination);
		NativeAssetCacheSnapshot Snapshot() const;
		void DropPages();
		void Unbind();

	private:
		struct State;
		std::unique_ptr<State> m_state;
	};
} // namespace AVPE
