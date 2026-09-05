// AVP:E native/optical byte-equivalence evidence. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssetByteTrace.h"

#include "AVPE/AVPE.h"
#include "AVPE/NativeConfig.h"
#include "CDVD/IsoReader.h"
#include "VMManager.h"

#include "Sha256.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cctype>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace AVPE::NativeAssetByteTrace
{
	namespace
	{
		constexpr std::string_view kSchema = "avpe-asset-byte-trace-v1";
		constexpr std::string_view kTargetSerial = "SLUS-20147";
		constexpr size_t kSectorSize = 2048;
		constexpr size_t kChunksPerFile = 16;
		constexpr size_t kMaximumFiles = 32;

		enum class Source : u8
		{
			IsoOracle = 1,
			NativeIoman = 2,
			NativeCdvd = 4,
		};

		struct ChunkAssembly
		{
			std::array<u8, kSectorSize> bytes{};
			std::bitset<kSectorSize> covered;
			u8 sources = 0;
			u64 hits = 0;
			bool conflict = false;
		};

		struct FileTrace
		{
			std::string path;
			u32 iso_lsn = 0;
			u32 iso_size = 0;
			std::array<ChunkAssembly, kChunksPerFile> chunks;
		};

		std::mutex s_mutex;
		std::vector<FileTrace> s_files;
		u64 s_dropped_files = 0;
		u64 s_dropped_bytes = 0;
		u64 s_registration_failures = 0;
		bool s_oracle_capture_attempted = false;

		std::optional<std::string_view> Mode()
		{
			return NativeConfig::AssetByteTraceMode();
		}

		std::string NormalizeGuestPath(const std::string_view path)
		{
			std::string normalized(path);
			std::replace(normalized.begin(), normalized.end(), '\\', '/');
			std::transform(normalized.begin(), normalized.end(), normalized.begin(),
				[](const unsigned char character) { return static_cast<char>(std::toupper(character)); });
			if (const size_t separator = normalized.find(':'); separator != std::string::npos)
				normalized.erase(0, separator + 1);
			while (!normalized.empty() && normalized.front() == '/')
				normalized.erase(normalized.begin());
			if (normalized.ends_with(";1"))
				normalized.resize(normalized.size() - 2);
			return normalized;
		}

		std::string IsoPath(const std::string_view normalized_guest_path)
		{
			return std::string(normalized_guest_path);
		}

		FileTrace* FindFile(const std::string_view normalized_path)
		{
			const auto found = std::find_if(s_files.begin(), s_files.end(),
				[normalized_path](const FileTrace& file) { return file.path == normalized_path; });
			return found == s_files.end() ? nullptr : &*found;
		}

		void AddBytes(FileTrace& file, const Source source, const u64 offset, const u8* bytes, const size_t size)
		{
			if (offset >= kChunksPerFile * kSectorSize)
				return;

			const size_t accepted = static_cast<size_t>(std::min<u64>(size, kChunksPerFile * kSectorSize - offset));
			std::array<bool, kChunksPerFile> touched{};
			for (size_t index = 0; index < accepted; ++index)
			{
				const size_t file_offset = static_cast<size_t>(offset) + index;
				const size_t chunk_index = file_offset / kSectorSize;
				ChunkAssembly& chunk = file.chunks[chunk_index];
				const size_t chunk_offset = file_offset % kSectorSize;
				if (chunk.covered.test(chunk_offset) && chunk.bytes[chunk_offset] != bytes[index])
					chunk.conflict = true;
				chunk.bytes[chunk_offset] = bytes[index];
				chunk.covered.set(chunk_offset);
				chunk.sources |= static_cast<u8>(source);
				touched[chunk_index] = true;
			}
			for (size_t index = 0; index < touched.size(); ++index)
			{
				if (touched[index])
					++file.chunks[index].hits;
			}
		}

		void RecordNativeBytes(const Source source, const std::string_view guest_path, const u64 offset,
			const void* bytes, const size_t size)
		{
			if (!IsEnabled() || Mode() != "native" || !bytes || size == 0)
				return;
			const std::string normalized = NormalizeGuestPath(guest_path);
			std::lock_guard lock(s_mutex);
			if (FileTrace* const file = FindFile(normalized))
				AddBytes(*file, source, offset, static_cast<const u8*>(bytes), size);
			else
				s_dropped_bytes += size;
		}

		std::string JsonEscape(const std::string_view value)
		{
			std::string escaped;
			escaped.reserve(value.size());
			for (const char character : value)
			{
				if (character == '\\' || character == '"')
					escaped += '\\';
				escaped += character;
			}
			return escaped;
		}

		std::string Sha256(const u8* bytes, const size_t size)
		{
			CSha256 state;
			std::array<Byte, SHA256_DIGEST_SIZE> digest{};
			Sha256_Init(&state);
			Sha256_Update(&state, bytes, size);
			Sha256_Final(&state, digest.data());
			constexpr char hex[] = "0123456789abcdef";
			std::string result(digest.size() * 2, '0');
			for (size_t index = 0; index < digest.size(); ++index)
			{
				result[index * 2] = hex[digest[index] >> 4];
				result[index * 2 + 1] = hex[digest[index] & 0x0f];
			}
			return result;
		}

		void AppendSources(std::string& body, const u8 sources)
		{
			body += '[';
			bool added = false;
			const auto append = [&body, &added](const std::string_view source) {
				if (added)
					body += ',';
				body += '"';
				body += source;
				body += '"';
				added = true;
			};
			if (sources & static_cast<u8>(Source::IsoOracle))
				append("iso-oracle");
			if (sources & static_cast<u8>(Source::NativeIoman))
				append("native-ioman");
			if (sources & static_cast<u8>(Source::NativeCdvd))
				append("native-cdvd");
			body += ']';
		}
	} // namespace

	bool IsEnabled()
	{
		return IsSurfacelessControlTest() && VMManager::GetDiscSerial() == kTargetSerial && Mode().has_value();
	}

	void RegisterOpticalFile(const std::string_view guest_path)
	{
		if (!IsEnabled())
			return;
		const std::string normalized = NormalizeGuestPath(guest_path);
		{
			std::lock_guard lock(s_mutex);
			if (FindFile(normalized))
				return;
			if (s_files.size() == kMaximumFiles)
			{
				++s_dropped_files;
				return;
			}
		}

		IsoReader reader;
		const bool opened = reader.Open();
		const std::optional<IsoReader::ISODirectoryEntry> entry =
			opened ? reader.LocateFile(IsoPath(normalized), nullptr) : std::nullopt;
		std::lock_guard lock(s_mutex);
		if (FindFile(normalized))
			return;
		if (!opened)
		{
			++s_registration_failures;
			return;
		}
		if (!entry || entry->length_le == 0)
			return;
		s_files.push_back({.path = normalized, .iso_lsn = entry->location_le, .iso_size = entry->length_le});
	}

	void RecordNativeIomanBytes(
		const std::string_view guest_path, const u64 offset, const void* bytes, const size_t size)
	{
		RecordNativeBytes(Source::NativeIoman, guest_path, offset, bytes, size);
	}

	void RecordNativeCdvdBytes(
		const std::string_view guest_path, const u64 offset, const void* bytes, const size_t size)
	{
		RecordNativeBytes(Source::NativeCdvd, guest_path, offset, bytes, size);
	}

	bool CaptureIsoOracle()
	{
		if (!IsEnabled() || Mode() != "oracle")
			return false;
		std::vector<std::string> paths;
		{
			std::lock_guard lock(s_mutex);
			if (s_oracle_capture_attempted)
				return s_registration_failures == 0;
			s_oracle_capture_attempted = true;
			paths.reserve(s_files.size());
			for (const FileTrace& file : s_files)
				paths.push_back(file.path);
		}

		for (const std::string& path : paths)
		{
			IsoReader reader;
			std::vector<u8> bytes;
			const bool opened = reader.Open();
			const std::optional<IsoReader::ISODirectoryEntry> entry =
				opened ? reader.LocateFile(IsoPath(path), nullptr) : std::nullopt;
			const bool read = entry && reader.ReadFile(*entry, &bytes);
			std::lock_guard lock(s_mutex);
			FileTrace* const file = FindFile(path);
			if (!opened || !read || !file || file->iso_lsn != entry->location_le || file->iso_size != entry->length_le)
			{
				++s_registration_failures;
				continue;
			}
			AddBytes(*file, Source::IsoOracle, 0, bytes.data(), bytes.size());
		}

		std::lock_guard lock(s_mutex);
		return s_registration_failures == 0;
	}

	std::string SnapshotJson()
	{
		const std::optional<std::string_view> mode = Mode();
		std::lock_guard lock(s_mutex);
		std::string body = "{\"schema\":\"" + std::string(kSchema) + "\",\"enabled\":";
		body += IsEnabled() ? "true" : "false";
		body += ",\"target_recognized\":";
		body += VMManager::GetDiscSerial() == kTargetSerial ? "true" : "false";
		body += ",\"mode\":\"" + std::string(mode.value_or("disabled")) + "\"";
		body += ",\"dropped_files\":" + std::to_string(s_dropped_files);
		body += ",\"dropped_bytes\":" + std::to_string(s_dropped_bytes);
		body += ",\"registration_failures\":" + std::to_string(s_registration_failures);
		body += ",\"files\":[";
		for (size_t file_index = 0; file_index < s_files.size(); ++file_index)
		{
			const FileTrace& file = s_files[file_index];
			if (file_index != 0)
				body += ',';
			body += "{\"path\":\"" + JsonEscape(file.path) + "\",\"iso_lsn\":" + std::to_string(file.iso_lsn);
			body += ",\"iso_size\":" + std::to_string(file.iso_size) + ",\"chunks\":[";
			bool added_chunk = false;
			for (size_t chunk_index = 0; chunk_index < file.chunks.size(); ++chunk_index)
			{
				const ChunkAssembly& chunk = file.chunks[chunk_index];
				const size_t offset = chunk_index * kSectorSize;
				if (offset >= file.iso_size)
					continue;
				const size_t chunk_size = static_cast<size_t>(std::min<u64>(kSectorSize, file.iso_size - offset));
				bool complete = chunk_size != 0;
				for (size_t byte_index = 0; complete && byte_index < chunk_size; ++byte_index)
					complete = chunk.covered.test(byte_index);
				if (!complete)
					continue;
				if (added_chunk)
					body += ',';
				body += "{\"offset\":" + std::to_string(offset) + ",\"size\":" + std::to_string(chunk_size);
				body += ",\"sha256\":\"" + Sha256(chunk.bytes.data(), chunk_size) + "\",\"sources\":";
				AppendSources(body, chunk.sources);
				body += ",\"hits\":" + std::to_string(chunk.hits) + ",\"conflict\":";
				body += chunk.conflict ? "true}" : "false}";
				added_chunk = true;
			}
			body += "]}";
		}
		body += "]}";
		return body;
	}

	void Reset()
	{
		std::lock_guard lock(s_mutex);
		s_files.clear();
		s_dropped_files = 0;
		s_dropped_bytes = 0;
		s_registration_failures = 0;
		s_oracle_capture_attempted = false;
	}
} // namespace AVPE::NativeAssetByteTrace
