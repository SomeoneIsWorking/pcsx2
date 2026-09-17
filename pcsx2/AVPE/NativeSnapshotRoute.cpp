// AVP:E diagnostic frame capture and BMP presentation. Fork-local.

#include "AVPE/NativeSnapshotRoute.h"

#include "MTGS.h"

#include <lucent/http.h>
#include <lucent/log.h>

#include <limits>
#include <string>

namespace AVPE::NativeSnapshotRoute
{
	std::optional<std::vector<u8>> EncodeBmp(const u32 width, const u32 height,
		const std::span<const u32> pixels)
	{
		if (width == 0 || height == 0 || pixels.size() != static_cast<size_t>(width) * height)
		{
			return std::nullopt;
		}
		const size_t row_bytes = static_cast<size_t>(width) * 3;
		const size_t row_pad = (4 - (row_bytes % 4)) % 4;
		if (row_bytes + row_pad > (std::numeric_limits<u32>::max() - 54) / height)
		{
			return std::nullopt;
		}
		const size_t data_size = (row_bytes + row_pad) * height;
		const u32 file_size = static_cast<u32>(54 + data_size);
		std::vector<u8> bmp(file_size);
		const auto put16 = [&](const size_t offset, const u16 value) {
			bmp[offset] = static_cast<u8>(value);
			bmp[offset + 1] = static_cast<u8>(value >> 8);
		};
		const auto put32 = [&](const size_t offset, const u32 value) {
			bmp[offset] = static_cast<u8>(value);
			bmp[offset + 1] = static_cast<u8>(value >> 8);
			bmp[offset + 2] = static_cast<u8>(value >> 16);
			bmp[offset + 3] = static_cast<u8>(value >> 24);
		};
		put16(0, 0x4D42);
		put32(2, file_size);
		put32(10, 54);
		put32(14, 40);
		put32(18, width);
		put32(22, height);
		put16(26, 1);
		put16(28, 24);
		put32(34, static_cast<u32>(data_size));
		size_t offset = 54;
		for (u32 y = height; y-- > 0;)
		{
			const u32* row = pixels.data() + static_cast<size_t>(y) * width;
			for (u32 x = 0; x < width; ++x)
			{
				const u32 pixel = row[x];
				bmp[offset++] = static_cast<u8>(pixel);
				bmp[offset++] = static_cast<u8>(pixel >> 8);
				bmp[offset++] = static_cast<u8>(pixel >> 16);
			}
			offset += row_pad;
		}
		return bmp;
	}

	lucent::http::Response Handle()
	{
		u32 width = 0;
		u32 height = 0;
		std::vector<u32> pixels;
		if (!MTGS::SaveMemorySnapshot(0, 0, true, false, &width, &height, &pixels))
		{
			return lucent::http::Response::text(503, "Unavailable", "no frame available\n");
		}
		if (width == 0 || height == 0)
		{
			return lucent::http::Response::text(500, "Error", "empty frame\n");
		}
		const auto bmp = EncodeBmp(width, height, pixels);
		if (!bmp)
		{
			return lucent::http::Response::text(500, "Error", "invalid frame size\n");
		}
		lucent::info("avpe", "snap {}x{}", width, height);
		return lucent::http::Response::binary(200, "OK", "image/bmp",
			std::string(reinterpret_cast<const char*>(bmp->data()), bmp->size()));
	}
} // namespace AVPE::NativeSnapshotRoute
