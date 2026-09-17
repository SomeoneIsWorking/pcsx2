// AVP:E diagnostic frame capture and BMP presentation. Fork-local.
#pragma once

#include "common/Pcsx2Defs.h"

#include <optional>
#include <span>
#include <vector>

namespace lucent::http
{
	struct Response;
} // namespace lucent::http

namespace AVPE::NativeSnapshotRoute
{
	std::optional<std::vector<u8>> EncodeBmp(u32 width, u32 height, std::span<const u32> pixels);
	lucent::http::Response Handle();
} // namespace AVPE::NativeSnapshotRoute
