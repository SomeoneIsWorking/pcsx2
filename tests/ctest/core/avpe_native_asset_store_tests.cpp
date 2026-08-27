// AVP:E validated native-asset store tests. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssetStore.h"

#include "Sha256.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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

	TEST_F(NativeAssetStoreTest, UnbindInvalidatesRecordGeneration)
	{
		const AVPE::NativeAssetStoreResult first = Resolve();
		ASSERT_EQ(first.disposition, AVPE::NativeAssetStoreDisposition::Found) << first.error;
		m_store.Unbind();
		const AVPE::NativeAssetStoreResult second = Resolve();
		ASSERT_EQ(second.disposition, AVPE::NativeAssetStoreDisposition::Found) << second.error;
		EXPECT_GT(second.record.generation, first.record.generation);
	}
} // namespace
