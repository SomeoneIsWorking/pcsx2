// AVP:E cache-backed ioman file adapter. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeAssetFile.h"

#include "AVPE/NativeAssets.h"
#include "IopBios.h"

#include <limits>
#include <new>
#include <utility>

namespace AVPE::NativeAssetFile
{
	namespace
	{
		class File final : public IOManFile
		{
		public:
			explicit File(NativeAssetStoreRecord record)
				: m_record(std::move(record))
			{
			}

			void close() override
			{
				delete this;
			}

			int lseek(const s32 offset, const s32 whence) override
			{
				s64 base = 0;
				switch (whence)
				{
					case IOP_SEEK_SET:
						break;
					case IOP_SEEK_CUR:
						base = static_cast<s64>(m_offset);
						break;
					case IOP_SEEK_END:
						base = static_cast<s64>(m_record.size);
						break;
					default:
						return -IOP_EIO;
				}

				const s64 position = base + offset;
				if (position < 0 || position > std::numeric_limits<s32>::max())
					return -IOP_EIO;
				m_offset = static_cast<u64>(position);
				return static_cast<s32>(position);
			}

			int read(void* const buffer, const u32 count) override /* Flawfinder: ignore */
			{
				if (count == 0)
					return 0;
				if (!buffer)
					return -IOP_EIO;

				const NativeAssets::ReadResult result = NativeAssets::Read(
					m_record, m_offset, std::span<u8>(static_cast<u8*>(buffer), count));
				if (result.disposition == NativeAssets::ReadDisposition::IoError ||
					result.bytes_read > static_cast<size_t>(std::numeric_limits<s32>::max()))
				{
					return -IOP_EIO;
				}
				m_offset += result.bytes_read;
				return static_cast<s32>(result.bytes_read);
			}

			int write(void*, u32) override
			{
				return -IOP_EROFS;
			}

		private:
			NativeAssetStoreRecord m_record;
			u64 m_offset = 0;
		};
	} // namespace

	int Open(IOManFile** const file, const NativeAssetStoreRecord& record)
	{
		if (!file || record.generation == 0 || record.path.empty() || record.size > std::numeric_limits<s32>::max())
			return -IOP_EIO;
		*file = new (std::nothrow) File(record);
		return *file ? 0 : -IOP_ENOMEM;
	}
} // namespace AVPE::NativeAssetFile
