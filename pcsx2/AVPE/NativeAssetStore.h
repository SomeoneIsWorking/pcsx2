// AVP:E validated native-asset store. Fork-local; not for upstream PCSX2.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace AVPE
{
	enum class NativeAssetStoreDisposition : std::uint8_t
	{
		Found,
		Missing,
		InvalidStore,
	};

	struct NativeAssetStoreRecord
	{
		std::uint32_t id = 0;
		std::uint64_t generation = 0;
		std::filesystem::path path;
		std::uint64_t size = 0;
		std::string sha256;
	};

	struct NativeAssetStoreResult
	{
		NativeAssetStoreDisposition disposition = NativeAssetStoreDisposition::InvalidStore;
		NativeAssetStoreRecord record;
		std::string error;
	};

	// Binds a files root only after its sibling manifest matches the supplied
	// admission digest. Requested paths are canonical uppercase relative paths;
	// the manifest retains ownership of their on-disk spelling.
	class NativeAssetStore final
	{
	public:
		NativeAssetStore();
		~NativeAssetStore();

		NativeAssetStore(const NativeAssetStore&) = delete;
		NativeAssetStore& operator=(const NativeAssetStore&) = delete;
		NativeAssetStore(NativeAssetStore&&) = delete;
		NativeAssetStore& operator=(NativeAssetStore&&) = delete;

		NativeAssetStoreResult Resolve(const std::filesystem::path& configured_files_root,
			std::string_view expected_manifest_sha256, std::string_view canonical_relative_path);
		void Unbind();

	private:
		struct State;
		std::unique_ptr<State> m_state;
	};
} // namespace AVPE
