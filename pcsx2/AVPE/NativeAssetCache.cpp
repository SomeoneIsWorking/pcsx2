// AVP:E bounded native-asset byte cache. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssetCache.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace AVPE
{
	namespace
	{
		constexpr std::size_t kMaximumCoalescedPages = 16;

		void Increment(std::uint64_t* const counter, const std::uint64_t amount = 1)
		{
			*counter = amount > std::numeric_limits<std::uint64_t>::max() - *counter ?
			               std::numeric_limits<std::uint64_t>::max() :
			               *counter + amount;
		}
	} // namespace

	struct NativeAssetCache::State
	{
		struct Key
		{
			std::uint64_t generation = 0;
			std::uint32_t record_id = 0;
			std::uint64_t page_index = 0;

			bool operator==(const Key&) const = default;
		};

		struct KeyHash
		{
			std::size_t operator()(const Key& key) const
			{
				std::size_t hash = std::hash<std::uint64_t>{}(key.generation);
				hash ^= std::hash<std::uint32_t>{}(key.record_id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
				hash ^= std::hash<std::uint64_t>{}(key.page_index) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
				return hash;
			}
		};

		struct Page
		{
			std::array<std::uint8_t, PageBytes> bytes{};
			std::size_t valid_bytes = 0;
		};

		struct Entry
		{
			std::unique_ptr<const Page> page;
			std::list<Key>::iterator lru_position;
		};

		mutable std::mutex mutex;
		std::optional<std::uint64_t> generation;
		std::list<Key> lru;
		std::unordered_map<Key, Entry, KeyHash> pages;
		std::uint64_t hits = 0;
		std::uint64_t misses = 0;
		std::uint64_t fills = 0;
		std::uint64_t evictions = 0;
		std::uint32_t transient_handles = 0;
		std::uint32_t peak_transient_handles = 0;

		void Clear(const bool unbind)
		{
			pages.clear();
			lru.clear();
			hits = 0;
			misses = 0;
			fills = 0;
			evictions = 0;
			transient_handles = 0;
			peak_transient_handles = 0;
			if (unbind)
				generation.reset();
		}

		void SelectGeneration(const std::uint64_t selected_generation)
		{
			if (!generation || *generation != selected_generation)
			{
				Clear(true);
				generation = selected_generation;
			}
		}

		void Touch(const typename std::unordered_map<Key, Entry, KeyHash>::iterator entry)
		{
			lru.splice(lru.begin(), lru, entry->second.lru_position);
			entry->second.lru_position = lru.begin();
		}

		void Insert(const Key& key, std::unique_ptr<const Page> page)
		{
			while (pages.size() >= MaximumPages)
			{
				const Key victim = lru.back();
				pages.erase(victim);
				lru.pop_back();
				Increment(&evictions);
			}
			lru.push_front(key);
			pages.emplace(key, Entry{.page = std::move(page), .lru_position = lru.begin()});
			Increment(&fills);
		}

		std::size_t ExpectedPageBytes(const NativeAssetStoreRecord& record, const std::uint64_t page_index) const
		{
			const std::uint64_t page_offset = page_index * PageBytes;
			return static_cast<std::size_t>(std::min<std::uint64_t>(PageBytes, record.size - page_offset));
		}

		std::vector<std::uint64_t> MissingRun(
			const NativeAssetStoreRecord& record, const std::uint64_t first_page, const std::uint64_t last_page)
		{
			std::vector<std::uint64_t> run;
			run.reserve(kMaximumCoalescedPages);
			for (std::uint64_t page = first_page;
				page <= last_page && run.size() < kMaximumCoalescedPages; ++page)
			{
				const Key key{record.generation, record.id, page};
				if (pages.contains(key))
					break;
				run.push_back(page);
			}
			Increment(&misses, run.size());
			return run;
		}

		NativeAssetCacheReadResult ReadAt(const NativeAssetStoreRecord& record, const std::uint64_t offset,
			const std::span<std::uint8_t> destination)
		{
			if (record.generation == 0 || record.path.empty())
				return {.error = "native asset cache received an unadmitted record"};
			SelectGeneration(record.generation);
			if (destination.empty())
				return {.disposition = NativeAssetCacheReadDisposition::Complete};
			if (offset >= record.size)
				return {.disposition = NativeAssetCacheReadDisposition::EndOfFile};

			const std::uint64_t logical_size = std::min<std::uint64_t>(destination.size(), record.size - offset);
			const std::uint64_t request_end = offset + logical_size;
			const std::uint64_t last_page = (request_end - 1) / PageBytes;
			std::size_t copied = 0;
			std::optional<std::ifstream> source;

			while (copied < logical_size)
			{
				const std::uint64_t current_offset = offset + copied;
				const std::uint64_t page_index = current_offset / PageBytes;
				const std::size_t page_offset = static_cast<std::size_t>(current_offset % PageBytes);
				const Key key{record.generation, record.id, page_index};
				const auto cached = pages.find(key);
				if (cached != pages.end())
				{
					if (page_offset >= cached->second.page->valid_bytes)
					{
						return {.disposition = copied == 0 ? NativeAssetCacheReadDisposition::EndOfFile :
						                                     NativeAssetCacheReadDisposition::ShortRead,
							.bytes_read = copied};
					}
					const std::size_t count = std::min<std::size_t>(
						cached->second.page->valid_bytes - page_offset, static_cast<std::size_t>(logical_size - copied));
					std::memcpy(destination.data() + copied, cached->second.page->bytes.data() + page_offset, count);
					copied += count;
					Increment(&hits);
					Touch(cached);
					continue;
				}

				const std::vector<std::uint64_t> run = MissingRun(record, page_index, last_page);
				if (run.empty())
					return {.bytes_read = copied, .error = "native asset cache could not form a missing-page run"};
				const std::uint64_t run_offset = run.front() * PageBytes;
				if (run_offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
					return {.bytes_read = copied, .error = "native asset offset exceeds host stream range"};

				std::size_t run_bytes = 0;
				for (const std::uint64_t run_page : run)
					run_bytes += ExpectedPageBytes(record, run_page);
				std::vector<std::uint8_t> buffer(run_bytes);

				if (!source)
				{
					source.emplace(record.path, std::ios::binary);
					if (!*source)
						return {.bytes_read = copied, .error = "native asset file could not be opened"};
					transient_handles = 1;
					peak_transient_handles = std::max(peak_transient_handles, transient_handles);
				}
				source->clear();
				source->seekg(static_cast<std::streamoff>(run_offset));
				if (!*source)
				{
					transient_handles = 0;
					return {.bytes_read = copied, .error = "native asset seek failed"};
				}
				source->read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
				const std::size_t source_bytes = static_cast<std::size_t>(source->gcount());
				const bool source_error = source->bad();

				std::size_t installed_bytes = 0;
				if (!source_error && source_bytes == run_bytes)
				{
					for (const std::uint64_t run_page : run)
					{
						const std::size_t page_bytes = ExpectedPageBytes(record, run_page);
						if (source_bytes - installed_bytes < page_bytes)
							break;
						auto page = std::make_unique<Page>();
						std::memcpy(page->bytes.data(), buffer.data() + installed_bytes, page_bytes);
						page->valid_bytes = page_bytes;
						Insert(Key{record.generation, record.id, run_page}, std::move(page));
						installed_bytes += page_bytes;
					}
				}

				const std::uint64_t available_end = run_offset + source_bytes;
				if (current_offset < available_end)
				{
					const std::size_t buffer_offset = static_cast<std::size_t>(current_offset - run_offset);
					const std::size_t count = static_cast<std::size_t>(
						std::min<std::uint64_t>(logical_size - copied, available_end - current_offset));
					std::memcpy(destination.data() + copied, buffer.data() + buffer_offset, count);
					copied += count;
				}

				if (source_error)
				{
					transient_handles = 0;
					return {.bytes_read = copied, .error = "native asset read failed"};
				}
				if (source_bytes != run_bytes)
				{
					const bool reached_eof = source->eof();
					transient_handles = 0;
					if (copied == logical_size)
					{
						return {
							.disposition = logical_size == destination.size() ?
						                       NativeAssetCacheReadDisposition::Complete :
						                       NativeAssetCacheReadDisposition::ShortRead,
							.bytes_read = copied,
						};
					}
					if (!reached_eof)
						return {.bytes_read = copied, .error = "native asset read failed"};
					return {.disposition = copied == 0 ? NativeAssetCacheReadDisposition::EndOfFile :
					                                     NativeAssetCacheReadDisposition::ShortRead,
						.bytes_read = copied};
				}
			}

			transient_handles = 0;
			return {
				.disposition = logical_size == destination.size() ? NativeAssetCacheReadDisposition::Complete :
			                                                        NativeAssetCacheReadDisposition::ShortRead,
				.bytes_read = copied,
			};
		}
	};

	NativeAssetCache::NativeAssetCache()
		: m_state(std::make_unique<State>())
	{
	}

	NativeAssetCache::~NativeAssetCache() = default;

	NativeAssetCacheReadResult NativeAssetCache::ReadAt(
		const NativeAssetStoreRecord& record, const std::uint64_t offset, const std::span<std::uint8_t> destination)
	{
		std::lock_guard lock(m_state->mutex);
		return m_state->ReadAt(record, offset, destination);
	}

	NativeAssetCacheSnapshot NativeAssetCache::Snapshot() const
	{
		std::lock_guard lock(m_state->mutex);
		return {
			.hits = m_state->hits,
			.misses = m_state->misses,
			.fills = m_state->fills,
			.evictions = m_state->evictions,
			.resident_pages = m_state->pages.size(),
			.resident_bytes = m_state->pages.size() * PageBytes,
			.transient_handles = m_state->transient_handles,
			.peak_transient_handles = m_state->peak_transient_handles,
		};
	}

	void NativeAssetCache::DropPages()
	{
		std::lock_guard lock(m_state->mutex);
		m_state->Clear(false);
	}

	void NativeAssetCache::Unbind()
	{
		std::lock_guard lock(m_state->mutex);
		m_state->Clear(true);
	}
} // namespace AVPE
