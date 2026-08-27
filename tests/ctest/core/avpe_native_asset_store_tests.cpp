// AVP:E validated native-asset store tests. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssetCache.h"
#include "AVPE/NativeAssetStore.h"

#include "Sha256.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr std::string_view kAssetBytes = "validated asset bytes";
	constexpr std::string_view kAssetPath = "TBD/TBF.TBF";

	std::string Sha256(const std::string_view bytes)
	{
		CSha256 state;
		std::array<Byte, SHA256_DIGEST_SIZE> digest{};
		Sha256_Init(&state);
		Sha256_Update(&state, reinterpret_cast<const Byte*>(bytes.data()), bytes.size());
		Sha256_Final(&state, digest.data());

		constexpr char hex[] = "0123456789abcdef";
		std::string result(digest.size() * 2, '0');
		for (std::size_t index = 0; index < digest.size(); ++index)
		{
			result[index * 2] = hex[digest[index] >> 4];
			result[index * 2 + 1] = hex[digest[index] & 0x0f];
		}
		return result;
	}

	std::string FileRecord(const std::string_view path, const std::string_view bytes)
	{
		return "{\"path\":\"" + std::string(path) + "\",\"size\":" + std::to_string(bytes.size()) +
		       ",\"sha256\":\"" + Sha256(bytes) + "\"}";
	}

	std::string Manifest(const std::string_view path, const std::string_view bytes)
	{
		return "{\"schema\":\"avpe-native-assets-v1\",\"files\":[" + FileRecord(path, bytes) + "]}";
	}

	void Write(const std::filesystem::path& path, const std::string_view bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		ASSERT_TRUE(output);
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		ASSERT_TRUE(output);
	}

	class NativeAssetStoreTest : public testing::Test
	{
	protected:
		void SetUp() override
		{
			m_store_root = std::filesystem::path(AVPE_TEST_SCRATCH_ROOT) / testing::UnitTest::GetInstance()->current_test_info()->name();
			std::filesystem::remove_all(m_store_root);
			m_files_root = m_store_root / "files";
			Write(m_files_root / kAssetPath, kAssetBytes);
			WriteManifest(Manifest(kAssetPath, kAssetBytes));
		}

		void TearDown() override
		{
			std::filesystem::remove_all(m_store_root);
		}

		void WriteManifest(const std::string& manifest)
		{
			m_manifest = manifest;
			m_manifest_digest = Sha256(manifest);
			Write(m_store_root / "manifest.json", manifest);
		}

		AVPE::NativeAssetStoreResult Resolve()
		{
			return m_store.Resolve(m_files_root, m_manifest_digest, kAssetPath);
		}

		AVPE::NativeAssetStoreResult Admit(const std::string_view bytes)
		{
			Write(m_files_root / kAssetPath, bytes);
			WriteManifest(Manifest(kAssetPath, bytes));
			m_store.Unbind();
			return Resolve();
		}

		AVPE::NativeAssetStore m_store;
		std::filesystem::path m_store_root;
		std::filesystem::path m_files_root;
		std::string m_manifest;
		std::string m_manifest_digest;
	};

	TEST_F(NativeAssetStoreTest, ResolvesOnlyValidatedManifestMember)
	{
		const AVPE::NativeAssetStoreResult found = Resolve();
		ASSERT_EQ(found.disposition, AVPE::NativeAssetStoreDisposition::Found) << found.error;
		EXPECT_EQ(found.record.size, kAssetBytes.size());
		EXPECT_EQ(found.record.path, std::filesystem::canonical(m_files_root / kAssetPath));

		const AVPE::NativeAssetStoreResult missing =
			m_store.Resolve(m_files_root, m_manifest_digest, "TBD/UNLISTED.TBD");
		EXPECT_EQ(missing.disposition, AVPE::NativeAssetStoreDisposition::Missing);
	}

	TEST_F(NativeAssetStoreTest, RejectsWrongAdmissionDigest)
	{
		const AVPE::NativeAssetStoreResult result = m_store.Resolve(m_files_root, std::string(64, '0'), kAssetPath);
		EXPECT_EQ(result.disposition, AVPE::NativeAssetStoreDisposition::InvalidStore);
	}

	TEST_F(NativeAssetStoreTest, RejectsUnsafeOrDuplicateManifestPaths)
	{
		WriteManifest("{\"schema\":\"avpe-native-assets-v1\",\"files\":[{\"path\":\"../TBF.TBF\",\"size\":1,\"sha256\":\"" +
					  std::string(64, '0') + "\"}]}");
		EXPECT_EQ(Resolve().disposition, AVPE::NativeAssetStoreDisposition::InvalidStore);

		WriteManifest("{\"schema\":\"avpe-native-assets-v1\",\"files\":[" +
					  FileRecord(kAssetPath, kAssetBytes) + "," + FileRecord("tbd/tbf.tbf", kAssetBytes) + "]}");
		EXPECT_EQ(Resolve().disposition, AVPE::NativeAssetStoreDisposition::InvalidStore);
	}

	TEST_F(NativeAssetStoreTest, RejectsWrongSizeAndSameSizeCorruption)
	{
		Write(m_files_root / kAssetPath, "short");
		EXPECT_EQ(Resolve().disposition, AVPE::NativeAssetStoreDisposition::InvalidStore);

		Write(m_files_root / kAssetPath, std::string(kAssetBytes.size(), 'x'));
		EXPECT_EQ(Resolve().disposition, AVPE::NativeAssetStoreDisposition::InvalidStore);
	}

	TEST_F(NativeAssetStoreTest, RevalidatesContentAfterMutation)
	{
		const AVPE::NativeAssetStoreResult admitted = Resolve();
		ASSERT_EQ(admitted.disposition, AVPE::NativeAssetStoreDisposition::Found) << admitted.error;

		const auto path = m_files_root / kAssetPath;
		const auto old_time = std::filesystem::last_write_time(path);
		Write(path, std::string(kAssetBytes.size(), 'x'));
		std::filesystem::last_write_time(path, old_time + std::chrono::seconds(2));

		EXPECT_EQ(Resolve().disposition, AVPE::NativeAssetStoreDisposition::InvalidStore);
	}

	TEST_F(NativeAssetStoreTest, RehashesManifestAfterAdmission)
	{
		const AVPE::NativeAssetStoreResult admitted = Resolve();
		ASSERT_EQ(admitted.disposition, AVPE::NativeAssetStoreDisposition::Found) << admitted.error;

		const auto path = m_store_root / "manifest.json";
		const auto old_time = std::filesystem::last_write_time(path);
		std::string changed = m_manifest;
		const std::size_t marker = changed.find("TBF.TBF");
		ASSERT_NE(marker, std::string::npos);
		changed[marker] = 'X';
		Write(path, changed);
		std::filesystem::last_write_time(path, old_time);

		EXPECT_EQ(Resolve().disposition, AVPE::NativeAssetStoreDisposition::InvalidStore);
	}

	TEST_F(NativeAssetStoreTest, StoreDriftRequiresExplicitUnbindBeforeRebinding)
	{
		const AVPE::NativeAssetStoreResult admitted = Resolve();
		ASSERT_EQ(admitted.disposition, AVPE::NativeAssetStoreDisposition::Found) << admitted.error;

		const std::string original_manifest = m_manifest;
		const std::string original_digest = m_manifest_digest;
		std::string changed = m_manifest;
		const std::size_t marker = changed.find("TBF.TBF");
		ASSERT_NE(marker, std::string::npos);
		changed[marker] = 'X';
		WriteManifest(changed);
		EXPECT_EQ(Resolve().disposition, AVPE::NativeAssetStoreDisposition::InvalidStore);

		Write(m_store_root / "manifest.json", original_manifest);
		m_manifest = original_manifest;
		m_manifest_digest = original_digest;
		EXPECT_EQ(Resolve().disposition, AVPE::NativeAssetStoreDisposition::InvalidStore);

		m_store.Unbind();
		EXPECT_EQ(Resolve().disposition, AVPE::NativeAssetStoreDisposition::Found);
	}

	TEST_F(NativeAssetStoreTest, UnbindInvalidatesRecordGeneration)
	{
		const AVPE::NativeAssetStoreResult first = Resolve();
		ASSERT_EQ(first.disposition, AVPE::NativeAssetStoreDisposition::Found) << first.error;
		m_store.Unbind();
		const AVPE::NativeAssetStoreResult second = Resolve();
		ASSERT_EQ(second.disposition, AVPE::NativeAssetStoreDisposition::Found) << second.error;
		EXPECT_GT(second.record.generation, first.record.generation);
	}

	TEST_F(NativeAssetStoreTest, CacheMatchesUnalignedMultiPageBytesAndReusesPages)
	{
		std::string bytes(AVPE::NativeAssetCache::PageBytes * 2 + 137, '\0');
		for (std::size_t index = 0; index < bytes.size(); ++index)
			bytes[index] = static_cast<char>(index % 251);
		const AVPE::NativeAssetStoreResult admitted = Admit(bytes);
		ASSERT_EQ(admitted.disposition, AVPE::NativeAssetStoreDisposition::Found) << admitted.error;

		AVPE::NativeAssetCache cache;
		const std::size_t offset = AVPE::NativeAssetCache::PageBytes - 19;
		std::vector<std::uint8_t> destination(AVPE::NativeAssetCache::PageBytes + 53, 0);
		const AVPE::NativeAssetCacheReadResult first = cache.ReadAt(admitted.record, offset, destination);
		ASSERT_EQ(first.disposition, AVPE::NativeAssetCacheReadDisposition::Complete) << first.error;
		EXPECT_EQ(first.bytes_read, destination.size());
		EXPECT_EQ(std::memcmp(destination.data(), bytes.data() + offset, destination.size()), 0);
		const AVPE::NativeAssetCacheSnapshot cold = cache.Snapshot();
		EXPECT_EQ(cold.misses, 3u);
		EXPECT_EQ(cold.fills, 3u);
		EXPECT_EQ(cold.peak_transient_handles, 1u);
		EXPECT_EQ(cold.transient_handles, 0u);

		std::fill(destination.begin(), destination.end(), 0);
		const AVPE::NativeAssetCacheReadResult second = cache.ReadAt(admitted.record, offset, destination);
		ASSERT_EQ(second.disposition, AVPE::NativeAssetCacheReadDisposition::Complete) << second.error;
		EXPECT_EQ(std::memcmp(destination.data(), bytes.data() + offset, destination.size()), 0);
		EXPECT_GT(cache.Snapshot().hits, cold.hits);
	}

	TEST_F(NativeAssetStoreTest, CacheDistinguishesShortReadAndEndOfFile)
	{
		const AVPE::NativeAssetStoreResult admitted = Resolve();
		ASSERT_EQ(admitted.disposition, AVPE::NativeAssetStoreDisposition::Found) << admitted.error;
		AVPE::NativeAssetCache cache;
		std::array<std::uint8_t, 8> destination{};
		destination.fill(0xcc);

		const AVPE::NativeAssetCacheReadResult short_read =
			cache.ReadAt(admitted.record, admitted.record.size - 3, destination);
		EXPECT_EQ(short_read.disposition, AVPE::NativeAssetCacheReadDisposition::ShortRead);
		EXPECT_EQ(short_read.bytes_read, 3u);
		EXPECT_TRUE(std::all_of(destination.begin() + 3, destination.end(), [](const std::uint8_t byte) {
			return byte == 0xcc;
		}));

		const auto before_eof = destination;
		const AVPE::NativeAssetCacheReadResult eof = cache.ReadAt(admitted.record, admitted.record.size, destination);
		EXPECT_EQ(eof.disposition, AVPE::NativeAssetCacheReadDisposition::EndOfFile);
		EXPECT_EQ(eof.bytes_read, 0u);
		EXPECT_EQ(destination, before_eof);
	}

	TEST_F(NativeAssetStoreTest, FailedCacheFillIsNotInstalledAndCanRetry)
	{
		std::string bytes(AVPE::NativeAssetCache::PageBytes * 2, 'v');
		const AVPE::NativeAssetStoreResult admitted = Admit(bytes);
		ASSERT_EQ(admitted.disposition, AVPE::NativeAssetStoreDisposition::Found) << admitted.error;
		AVPE::NativeAssetCache cache;
		std::vector<std::uint8_t> destination(bytes.size());

		Write(m_files_root / kAssetPath, std::string(AVPE::NativeAssetCache::PageBytes, 'v'));
		const AVPE::NativeAssetCacheReadResult failed = cache.ReadAt(admitted.record, 0, destination);
		EXPECT_NE(failed.disposition, AVPE::NativeAssetCacheReadDisposition::Complete);
		EXPECT_EQ(cache.Snapshot().fills, 0u);

		Write(m_files_root / kAssetPath, bytes);
		const AVPE::NativeAssetCacheReadResult retry = cache.ReadAt(admitted.record, 0, destination);
		ASSERT_EQ(retry.disposition, AVPE::NativeAssetCacheReadDisposition::Complete) << retry.error;
		EXPECT_EQ(cache.Snapshot().fills, 2u);
	}

	TEST_F(NativeAssetStoreTest, CacheEnforcesExactCapacityAndTrueLru)
	{
		std::string bytes(AVPE::NativeAssetCache::PageBytes * (AVPE::NativeAssetCache::MaximumPages + 1), '\0');
		for (std::size_t page = 0; page <= AVPE::NativeAssetCache::MaximumPages; ++page)
			std::fill_n(bytes.begin() + page * AVPE::NativeAssetCache::PageBytes,
				AVPE::NativeAssetCache::PageBytes, static_cast<char>(page % 251));
		const AVPE::NativeAssetStoreResult admitted = Admit(bytes);
		ASSERT_EQ(admitted.disposition, AVPE::NativeAssetStoreDisposition::Found) << admitted.error;

		AVPE::NativeAssetCache cache;
		std::array<std::uint8_t, 1> byte{};
		for (std::size_t page = 0; page < AVPE::NativeAssetCache::MaximumPages; ++page)
		{
			ASSERT_EQ(cache.ReadAt(admitted.record, page * AVPE::NativeAssetCache::PageBytes, byte).disposition,
				AVPE::NativeAssetCacheReadDisposition::Complete);
		}
		ASSERT_EQ(cache.ReadAt(admitted.record, 0, byte).disposition,
			AVPE::NativeAssetCacheReadDisposition::Complete);
		ASSERT_EQ(cache.ReadAt(admitted.record, AVPE::NativeAssetCache::MaximumPages * AVPE::NativeAssetCache::PageBytes,
						   byte)
					  .disposition,
			AVPE::NativeAssetCacheReadDisposition::Complete);

		const AVPE::NativeAssetCacheSnapshot full = cache.Snapshot();
		EXPECT_EQ(full.resident_pages, AVPE::NativeAssetCache::MaximumPages);
		EXPECT_EQ(full.resident_bytes, AVPE::NativeAssetCache::MaximumResidentBytes);
		EXPECT_EQ(full.evictions, 1u);
		const std::uint64_t misses_before_victim = full.misses;
		ASSERT_EQ(cache.ReadAt(admitted.record, AVPE::NativeAssetCache::PageBytes, byte).disposition,
			AVPE::NativeAssetCacheReadDisposition::Complete);
		EXPECT_GT(cache.Snapshot().misses, misses_before_victim);
	}

	TEST_F(NativeAssetStoreTest, CacheDropAndGenerationChangeHaveDistinctEffects)
	{
		AVPE::NativeAssetStoreResult admitted = Resolve();
		ASSERT_EQ(admitted.disposition, AVPE::NativeAssetStoreDisposition::Found) << admitted.error;
		AVPE::NativeAssetCache cache;
		std::array<std::uint8_t, 1> byte{};
		ASSERT_EQ(cache.ReadAt(admitted.record, 0, byte).disposition,
			AVPE::NativeAssetCacheReadDisposition::Complete);
		cache.DropPages();
		EXPECT_EQ(cache.Snapshot().resident_pages, 0u);
		ASSERT_EQ(cache.ReadAt(admitted.record, 0, byte).disposition,
			AVPE::NativeAssetCacheReadDisposition::Complete);

		m_store.Unbind();
		admitted = Resolve();
		ASSERT_EQ(admitted.disposition, AVPE::NativeAssetStoreDisposition::Found) << admitted.error;
		ASSERT_EQ(cache.ReadAt(admitted.record, 0, byte).disposition,
			AVPE::NativeAssetCacheReadDisposition::Complete);
		const AVPE::NativeAssetCacheSnapshot rebound = cache.Snapshot();
		EXPECT_EQ(rebound.resident_pages, 1u);
		EXPECT_EQ(rebound.misses, 1u);
		EXPECT_EQ(rebound.fills, 1u);
	}
} // namespace
