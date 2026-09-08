// AVP:E native profile persistence seam. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeSaveBackend.h"

#include "AVPE/NativeProfileContract.h"
#include "Config.h"
#include "VMManager.h"
#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "Sha256.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AVPE::NativeSaveBackend
{
	namespace
	{
		constexpr std::string_view kSchema = "avpe-native-save-v1";
		constexpr std::string_view kSerial = "SLUS-20147";
		constexpr u32 kCrc = 0x64DA78A3;
		constexpr u32 kProfileRevision = 0x1CD9DEE3;
		constexpr u32 kProfileSize = 0x20;
		constexpr u32 kProfileSlotCount = 4;

		std::mutex s_mutex;
		std::optional<NativeProfileContract::Snapshot> s_pending_profile;

		std::filesystem::path SavePath()
		{
			return std::filesystem::path(EmuFolders::DataRoot) / "AVPE" / "avpe-saves.avpesave";
		}

		std::string Hex(const u8* bytes, const size_t size)
		{
			constexpr char digits[] = "0123456789abcdef";
			std::string result(size * 2, '0');
			for (size_t index = 0; index < size; ++index)
			{
				result[index * 2] = digits[bytes[index] >> 4];
				result[index * 2 + 1] = digits[bytes[index] & 0x0f];
			}
			return result;
		}

		std::string Digest(const u8* bytes, const size_t size)
		{
			CSha256 state;
			Sha256_Init(&state);
			Sha256_Update(&state, bytes, size);
			std::array<Byte, SHA256_DIGEST_SIZE> digest{};
			Sha256_Final(&state, digest.data());
			return Hex(digest.data(), digest.size());
		}

		bool DecodeHex(const std::string_view encoded, std::vector<u8>* bytes)
		{
			if (encoded.size() % 2 != 0)
				return false;
			bytes->assign(encoded.size() / 2, 0);
			for (size_t index = 0; index < bytes->size(); ++index)
			{
				auto nibble = [](const char value) -> int {
					if (value >= '0' && value <= '9')
						return value - '0';
					if (value >= 'a' && value <= 'f')
						return value - 'a' + 10;
					if (value >= 'A' && value <= 'F')
						return value - 'A' + 10;
					return -1;
				};
				const int high = nibble(encoded[index * 2]);
				const int low = nibble(encoded[index * 2 + 1]);
				if (high < 0 || low < 0)
					return false;
				(*bytes)[index] = static_cast<u8>((high << 4) | low);
			}
			return true;
		}

		bool ReadDocument(const std::filesystem::path& path, rapidjson::Document* document)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return false;
			const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			if (bytes.size() > 4 * 1024 * 1024)
				return false;
			document->Parse(bytes.data(), bytes.size());
			return !document->HasParseError() && document->IsObject();
		}

		bool ValidateContainer(const rapidjson::Document& document)
		{
			const auto schema = document.FindMember("schema");
			const auto title = document.FindMember("title");
			const auto slots = document.FindMember("slots");
			if (schema == document.MemberEnd() || !schema->value.IsString() ||
				std::string_view(schema->value.GetString(), schema->value.GetStringLength()) != kSchema ||
				title == document.MemberEnd() || !title->value.IsObject() || slots == document.MemberEnd() ||
				!slots->value.IsObject())
				return false;
			const auto serial = title->value.FindMember("serial");
			const auto crc = title->value.FindMember("crc");
			return serial != title->value.MemberEnd() && serial->value.IsString() &&
			       std::string_view(serial->value.GetString(), serial->value.GetStringLength()) == kSerial &&
			       crc != title->value.MemberEnd() && crc->value.IsUint() && crc->value.GetUint() == kCrc;
		}

		bool ApplyProfile(rapidjson::Document& document, const NativeProfileContract::Snapshot& snapshot)
		{
			if (snapshot.payload.size() != kProfileSize || snapshot.revision != kProfileRevision ||
				snapshot.slot_count != kProfileSlotCount)
				return false;
			auto& allocator = document.GetAllocator();
			rapidjson::Value profile(rapidjson::kObjectType);
			const std::string payload_hex = Hex(snapshot.payload.data(), snapshot.payload.size());
			const std::string sha256 = Digest(snapshot.payload.data(), snapshot.payload.size());
			profile.AddMember("payload_hex", rapidjson::Value(payload_hex.c_str(), allocator), allocator);
			profile.AddMember("revision", snapshot.revision, allocator);
			profile.AddMember("sha256", rapidjson::Value(sha256.c_str(), allocator), allocator);
			profile.AddMember("slot_count", snapshot.slot_count, allocator);
			auto existing = document.FindMember("profile");
			if (existing == document.MemberEnd())
				document.AddMember("profile", std::move(profile), allocator);
			else
				existing->value = std::move(profile);
			return true;
		}

		bool WriteDocument(const std::filesystem::path& path, const rapidjson::Document& document)
		{
			if (path.parent_path().empty())
				return false;
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			if (error)
				return false;
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			if (!document.Accept(writer))
				return false;
			const std::filesystem::path temporary = path.string() + ".tmp";
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output)
					return false;
				output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
				output.flush();
				if (!output)
					return false;
			}
			Error rename_error;
			return FileSystem::RenamePath(temporary.string().c_str(), path.string().c_str(), &rename_error);
		}

	} // namespace

	static bool RestoreProfile(const NativeProfileContract::Snapshot& snapshot);

	static bool PersistProfileSave(const NativeProfileContract::Snapshot& snapshot, const s32 result)
	{
		if (VMManager::GetDiscSerial() != kSerial || VMManager::GetDiscCRC() != kCrc || result != 0)
			return false;
		const std::filesystem::path path = SavePath();
		rapidjson::Document document;
		if (std::filesystem::exists(path))
		{
			if (!ReadDocument(path, &document) || !ValidateContainer(document))
			{
				Console.Error("AVPE native profile save refused an invalid native container");
				return false;
			}
		}
		else
		{
			document.SetObject();
			auto& allocator = document.GetAllocator();
			document.AddMember("schema", rapidjson::Value(kSchema.data(), allocator), allocator);
			rapidjson::Value title(rapidjson::kObjectType);
			title.AddMember("serial", rapidjson::Value(kSerial.data(), allocator), allocator);
			title.AddMember("crc", kCrc, allocator);
			document.AddMember("title", std::move(title), allocator);
			document.AddMember("profile", rapidjson::Value(rapidjson::kNullType), allocator);
			document.AddMember("slots", rapidjson::Value(rapidjson::kObjectType), allocator);
		}
		if (!ApplyProfile(document, snapshot) || !WriteDocument(path, document))
		{
			Console.Error("AVPE native profile save could not atomically persist the profile");
			return false;
		}
		return true;
	}

	void ObserveProfileEntry(const NativeProfileContract::Snapshot& snapshot)
	{
		std::lock_guard lock(s_mutex);
		s_pending_profile = snapshot;
	}

	void ObserveProfileReturn(const s32 result)
	{
		std::optional<NativeProfileContract::Snapshot> snapshot;
		{
			std::lock_guard lock(s_mutex);
			snapshot = std::move(s_pending_profile);
		}
		if (snapshot)
			PersistProfileSave(*snapshot, result);
	}

	void ObserveProfileLoadEntry(const NativeProfileContract::Snapshot& snapshot)
	{
		RestoreProfile(snapshot);
	}

	static bool RestoreProfile(const NativeProfileContract::Snapshot& snapshot)
	{
		if (VMManager::GetDiscSerial() != kSerial || VMManager::GetDiscCRC() != kCrc)
			return false;
		const std::filesystem::path path = SavePath();
		if (!std::filesystem::exists(path))
			return false;
		rapidjson::Document document;
		if (!ReadDocument(path, &document) || !ValidateContainer(document))
			return false;
		const auto profile = document.FindMember("profile");
		if (profile == document.MemberEnd() || !profile->value.IsObject())
			return false;
		const auto revision = profile->value.FindMember("revision");
		const auto slot_count = profile->value.FindMember("slot_count");
		const auto payload = profile->value.FindMember("payload_hex");
		const auto sha256 = profile->value.FindMember("sha256");
		if (revision == profile->value.MemberEnd() || !revision->value.IsUint() ||
			revision->value.GetUint() != snapshot.revision || slot_count == profile->value.MemberEnd() ||
			!slot_count->value.IsUint() || slot_count->value.GetUint() != snapshot.slot_count ||
			payload == profile->value.MemberEnd() || !payload->value.IsString() ||
			sha256 == profile->value.MemberEnd() || !sha256->value.IsString())
			return false;
		std::vector<u8> bytes;
		if (!DecodeHex({payload->value.GetString(), payload->value.GetStringLength()}, &bytes) ||
			bytes.size() != snapshot.size || Digest(bytes.data(), bytes.size()) != std::string(sha256->value.GetString(), sha256->value.GetStringLength()))
			return false;
		NativeProfileContract::Snapshot restored = snapshot;
		restored.payload = std::move(bytes);
		return NativeProfileContract::RestorePayload(restored);
	}
} // namespace AVPE::NativeSaveBackend
