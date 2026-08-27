// AVP:E validated native-asset store. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssetStore.h"

#include "Sha256.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace AVPE
{
	namespace
	{
		constexpr std::string_view kManifestName = "manifest.json";
		constexpr std::string_view kStoreSchema = "avpe-native-assets-v1";
		constexpr std::uintmax_t kMaximumManifestBytes = 4 * 1024 * 1024;
		constexpr std::size_t kHashBufferBytes = 1024 * 1024;

		bool IsLowerHexDigest(const std::string_view value)
		{
			return value.size() == SHA256_DIGEST_SIZE * 2 &&
			       std::all_of(value.begin(), value.end(), [](const char character) {
					   return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
				   });
		}

		std::optional<std::string> CanonicalPathKey(const std::string_view path, const bool require_uppercase)
		{
			if (path.empty() || path.front() == '/' || path.back() == '/' || path.find('\\') != std::string_view::npos ||
				path.find(':') != std::string_view::npos || path.find('\0') != std::string_view::npos)
			{
				return std::nullopt;
			}

			std::string key;
			key.reserve(path.size());
			std::size_t component_start = 0;
			for (std::size_t index = 0; index <= path.size(); ++index)
			{
				if (index != path.size() && path[index] != '/')
					continue;
				const std::string_view component = path.substr(component_start, index - component_start);
				if (component.empty() || component == "." || component == "..")
					return std::nullopt;
				component_start = index + 1;
			}

			for (const char character : path)
			{
				const unsigned char byte = static_cast<unsigned char>(character);
				if (byte >= 0x80 || std::iscntrl(byte))
					return std::nullopt;
				if (character >= 'a' && character <= 'z')
				{
					if (require_uppercase)
						return std::nullopt;
					key.push_back(static_cast<char>(character - 'a' + 'A'));
				}
				else
				{
					key.push_back(character);
				}
			}
			return key;
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

		std::string DigestHex(const std::array<Byte, SHA256_DIGEST_SIZE>& digest)
		{
			constexpr char hex[] = "0123456789abcdef";
			std::string result(digest.size() * 2, '0');
			for (std::size_t index = 0; index < digest.size(); ++index)
			{
				result[index * 2] = hex[digest[index] >> 4];
				result[index * 2 + 1] = hex[digest[index] & 0x0f];
			}
			return result;
		}

		std::optional<std::string> HashFile(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return std::nullopt;

			CSha256 state;
			Sha256_Init(&state);
			std::array<Byte, kHashBufferBytes> buffer{};
			for (;;)
			{
				input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
				const std::streamsize count = input.gcount();
				if (count > 0)
					Sha256_Update(&state, buffer.data(), static_cast<std::size_t>(count));
				if (input.eof())
					break;
				if (!input)
					return std::nullopt;
			}

			std::array<Byte, SHA256_DIGEST_SIZE> digest{};
			Sha256_Final(&state, digest.data());
			return DigestHex(digest);
		}

		struct ManifestBytes
		{
			std::string bytes;
			std::string sha256;
		};

		std::optional<ManifestBytes> ReadManifest(const std::filesystem::path& path)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(path, error) || error)
				return std::nullopt;
			const std::uintmax_t size = std::filesystem::file_size(path, error);
			if (error || size == 0 || size > kMaximumManifestBytes ||
				size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
			{
				return std::nullopt;
			}
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return std::nullopt;
			std::string bytes(static_cast<std::size_t>(size), '\0');
			if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
				return std::nullopt;

			CSha256 state;
			std::array<Byte, SHA256_DIGEST_SIZE> digest{};
			Sha256_Init(&state);
			Sha256_Update(&state, reinterpret_cast<const Byte*>(bytes.data()), bytes.size());
			Sha256_Final(&state, digest.data());
			return ManifestBytes{
				.bytes = std::move(bytes),
				.sha256 = DigestHex(digest),
			};
		}
	} // namespace

	struct NativeAssetStore::State
	{
		struct Record
		{
			std::uint32_t id = 0;
			std::filesystem::path relative_path;
			std::uint64_t size = 0;
			std::string sha256;
			bool content_validated = false;
			std::filesystem::path validated_path;
			std::uintmax_t validated_size = 0;
			std::filesystem::file_time_type validated_last_write_time;
		};

		std::mutex mutex;
		std::filesystem::path root;
		std::filesystem::path manifest_path;
		std::string manifest_sha256;
		std::uint64_t generation = 0;
		bool bound = false;
		bool rebind_blocked = false;
		std::vector<Record> records;
		std::unordered_map<std::string, std::uint32_t> records_by_key;

		void ClearBinding()
		{
			root.clear();
			manifest_path.clear();
			manifest_sha256.clear();
			bound = false;
			records.clear();
			records_by_key.clear();
		}

		void ExplicitlyUnbind()
		{
			ClearBinding();
			rebind_blocked = false;
			++generation;
		}

		void BlockRebind()
		{
			ClearBinding();
			rebind_blocked = true;
			++generation;
		}

		bool ManifestUnchanged() const
		{
			const std::optional<ManifestBytes> manifest = ReadManifest(manifest_path);
			return manifest && manifest->sha256 == manifest_sha256;
		}

		bool Bind(const std::filesystem::path& canonical_root, const std::string_view expected_digest,
			std::string* error_message)
		{
			const std::filesystem::path candidate_manifest = canonical_root.parent_path() / kManifestName;
			std::optional<ManifestBytes> manifest = ReadManifest(candidate_manifest);
			if (!manifest)
			{
				*error_message = "native asset manifest is missing or unreadable";
				return false;
			}
			if (manifest->sha256 != expected_digest)
			{
				*error_message = "native asset manifest admission digest does not match";
				return false;
			}

			rapidjson::Document document;
			document.Parse(manifest->bytes.data(), manifest->bytes.size());
			if (document.HasParseError() || !document.IsObject())
			{
				*error_message = "native asset manifest is not valid JSON";
				return false;
			}
			const auto schema = document.FindMember("schema");
			if (schema == document.MemberEnd() || !schema->value.IsString() ||
				std::string_view(schema->value.GetString(), schema->value.GetStringLength()) != kStoreSchema)
			{
				*error_message = "native asset manifest has the wrong schema";
				return false;
			}
			const auto files = document.FindMember("files");
			if (files == document.MemberEnd() || !files->value.IsArray() || files->value.Empty() ||
				files->value.Size() > std::numeric_limits<std::uint32_t>::max())
			{
				*error_message = "native asset manifest has no valid file array";
				return false;
			}

			std::vector<Record> parsed_records;
			std::unordered_map<std::string, std::uint32_t> parsed_keys;
			parsed_records.reserve(files->value.Size());
			parsed_keys.reserve(files->value.Size());
			for (const rapidjson::Value& value : files->value.GetArray())
			{
				if (!value.IsObject())
				{
					*error_message = "native asset manifest contains a non-object file record";
					return false;
				}
				const auto path_member = value.FindMember("path");
				const auto size_member = value.FindMember("size");
				const auto hash_member = value.FindMember("sha256");
				if (path_member == value.MemberEnd() || !path_member->value.IsString() ||
					size_member == value.MemberEnd() || !size_member->value.IsUint64() ||
					hash_member == value.MemberEnd() || !hash_member->value.IsString())
				{
					*error_message = "native asset manifest contains an incomplete file record";
					return false;
				}

				const std::string relative_path(path_member->value.GetString(), path_member->value.GetStringLength());
				const std::optional<std::string> key = CanonicalPathKey(relative_path, false);
				const std::string sha256(hash_member->value.GetString(), hash_member->value.GetStringLength());
				if (!key || !IsLowerHexDigest(sha256))
				{
					*error_message = "native asset manifest contains an unsafe path or invalid digest";
					return false;
				}

				const std::uint32_t id = static_cast<std::uint32_t>(parsed_records.size());
				if (!parsed_keys.emplace(*key, id).second)
				{
					*error_message = "native asset manifest contains case-insensitive duplicate paths";
					return false;
				}
				parsed_records.push_back({
					.id = id,
					.relative_path = std::filesystem::path(relative_path),
					.size = size_member->value.GetUint64(),
					.sha256 = sha256,
				});
			}

			root = canonical_root;
			manifest_path = candidate_manifest;
			manifest_sha256 = std::string(expected_digest);
			records = std::move(parsed_records);
			records_by_key = std::move(parsed_keys);
			bound = true;
			++generation;
			return true;
		}

		NativeAssetStoreResult ValidateRecord(Record& record)
		{
			std::error_code filesystem_error;
			const std::filesystem::path candidate = std::filesystem::canonical(root / record.relative_path, filesystem_error);
			if (filesystem_error || !IsDescendant(root, candidate) ||
				!std::filesystem::is_regular_file(candidate, filesystem_error) || filesystem_error)
			{
				record.content_validated = false;
				return {.disposition = NativeAssetStoreDisposition::InvalidStore,
					.error = "manifest asset is missing, unsafe, or not a regular file"};
			}

			const std::uintmax_t size = std::filesystem::file_size(candidate, filesystem_error);
			if (filesystem_error || size != record.size)
			{
				record.content_validated = false;
				return {.disposition = NativeAssetStoreDisposition::InvalidStore,
					.error = "manifest asset size does not match"};
			}
			const std::filesystem::file_time_type last_write_time =
				std::filesystem::last_write_time(candidate, filesystem_error);
			if (filesystem_error)
			{
				record.content_validated = false;
				return {.disposition = NativeAssetStoreDisposition::InvalidStore,
					.error = "manifest asset timestamp is unreadable"};
			}

			if (!record.content_validated || record.validated_path != candidate || record.validated_size != size ||
				record.validated_last_write_time != last_write_time)
			{
				const std::optional<std::string> content_hash = HashFile(candidate);
				std::error_code post_hash_error;
				const std::uintmax_t post_hash_size = std::filesystem::file_size(candidate, post_hash_error);
				const std::filesystem::file_time_type post_hash_time =
					std::filesystem::last_write_time(candidate, post_hash_error);
				if (!content_hash || post_hash_error || post_hash_size != size || post_hash_time != last_write_time ||
					*content_hash != record.sha256)
				{
					record.content_validated = false;
					return {.disposition = NativeAssetStoreDisposition::InvalidStore,
						.error = "manifest asset content hash does not match"};
				}
				record.content_validated = true;
				record.validated_path = candidate;
				record.validated_size = size;
				record.validated_last_write_time = last_write_time;
			}

			return {
				.disposition = NativeAssetStoreDisposition::Found,
				.record = {
					.id = record.id,
					.generation = generation,
					.path = candidate,
					.size = record.size,
					.sha256 = record.sha256,
				},
			};
		}

		NativeAssetStoreResult Validate(const NativeAssetStoreRecord& admitted_record)
		{
			if (!bound || rebind_blocked || admitted_record.generation != generation ||
				admitted_record.id >= records.size())
			{
				return {.disposition = NativeAssetStoreDisposition::InvalidStore,
					.error = "native asset record is stale or the store is unbound"};
			}
			if (!ManifestUnchanged())
			{
				BlockRebind();
				return {.disposition = NativeAssetStoreDisposition::InvalidStore,
					.error = "native asset manifest changed without an explicit unbind"};
			}

			Record& record = records[admitted_record.id];
			if (admitted_record.path != record.validated_path || admitted_record.size != record.size ||
				admitted_record.sha256 != record.sha256)
			{
				return {.disposition = NativeAssetStoreDisposition::InvalidStore,
					.error = "native asset record identity does not match the bound store"};
			}
			return ValidateRecord(record);
		}

		NativeAssetStoreResult Resolve(const std::filesystem::path& configured_files_root,
			const std::string_view expected_digest, const std::string_view canonical_relative_path)
		{
			const std::optional<std::string> requested_key = CanonicalPathKey(canonical_relative_path, true);
			if (!requested_key)
				return {.disposition = NativeAssetStoreDisposition::Missing, .error = "asset path is not canonical"};
			if (!IsLowerHexDigest(expected_digest))
			{
				if (bound)
					BlockRebind();
				return {.disposition = NativeAssetStoreDisposition::InvalidStore,
					.error = "manifest admission digest is not lowercase SHA-256"};
			}

			std::error_code filesystem_error;
			const std::filesystem::path canonical_root = std::filesystem::canonical(configured_files_root, filesystem_error);
			if (filesystem_error || !std::filesystem::is_directory(canonical_root, filesystem_error) || filesystem_error)
			{
				if (bound)
					BlockRebind();
				return {.disposition = NativeAssetStoreDisposition::InvalidStore,
					.error = "native asset files root is missing or invalid"};
			}

			if (bound && (root != canonical_root || manifest_sha256 != expected_digest || !ManifestUnchanged()))
				BlockRebind();
			if (rebind_blocked)
			{
				return {.disposition = NativeAssetStoreDisposition::InvalidStore,
					.error = "native asset store changed without an explicit unbind"};
			}
			if (!bound)
			{
				std::string bind_error;
				if (!Bind(canonical_root, expected_digest, &bind_error))
					return {.disposition = NativeAssetStoreDisposition::InvalidStore, .error = std::move(bind_error)};
			}

			const auto indexed = records_by_key.find(*requested_key);
			if (indexed == records_by_key.end())
				return {.disposition = NativeAssetStoreDisposition::Missing, .error = "asset is absent from manifest"};
			Record& record = records[indexed->second];
			return ValidateRecord(record);
		}
	};

	NativeAssetStore::NativeAssetStore()
		: m_state(std::make_unique<State>())
	{
	}

	NativeAssetStore::~NativeAssetStore() = default;

	NativeAssetStoreResult NativeAssetStore::Resolve(const std::filesystem::path& configured_files_root,
		const std::string_view expected_manifest_sha256, const std::string_view canonical_relative_path)
	{
		std::lock_guard lock(m_state->mutex);
		return m_state->Resolve(configured_files_root, expected_manifest_sha256, canonical_relative_path);
	}

	NativeAssetStoreResult NativeAssetStore::Validate(const NativeAssetStoreRecord& admitted_record)
	{
		std::lock_guard lock(m_state->mutex);
		return m_state->Validate(admitted_record);
	}

	void NativeAssetStore::Unbind()
	{
		std::lock_guard lock(m_state->mutex);
		m_state->ExplicitlyUnbind();
	}
} // namespace AVPE
