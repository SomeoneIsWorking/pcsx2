// AVP:E live CProfile contract. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeProfileContract.h"

#include "AVPE/GuestObjects.h"
#include "R5900.h"
#include "VMManager.h"
#include "vtlb.h"

#include <optional>
#include <string_view>

namespace AVPE::NativeProfileContract
{
	namespace
	{
		constexpr std::string_view kTargetSerial = "SLUS-20147";
		constexpr u32 kTargetCrc = 0x64DA78A3;
		constexpr u32 kProfileSingletonAddress = 0x0036703C;
		constexpr u32 kMaximumPayloadBytes = 0x2000;
	} // namespace

	std::optional<Snapshot> CaptureObject(const u32 object)
	{
		if (object == 0 || !GuestObjects::IsPlausibleAddress(object))
			return std::nullopt;

		Snapshot snapshot;
		snapshot.object = object;
		if (!GuestObjects::ReadWord(object + 0x18, &snapshot.data) ||
			!GuestObjects::ReadWord(object + 0x1C, &snapshot.size) ||
			!GuestObjects::ReadWord(object + 0x20, &snapshot.revision) ||
			!GuestObjects::ReadWord(object + 0x24, &snapshot.slot_count) || snapshot.data == 0 ||
			snapshot.size == 0 || snapshot.size > kMaximumPayloadBytes ||
			!GuestObjects::IsPlausibleAddress(snapshot.data))
			return std::nullopt;

		snapshot.payload.resize(snapshot.size);
		if (!GuestObjects::ReadBytes(snapshot.data, snapshot.payload.data(), snapshot.size))
			return std::nullopt;
		return snapshot;
	}

	std::optional<Snapshot> CaptureCurrent()
	{
		if (VMManager::GetDiscSerial() != kTargetSerial || VMManager::GetDiscCRC() != kTargetCrc)
			return std::nullopt;

		u32 profile = 0;
		if (!GuestObjects::ReadWord(kProfileSingletonAddress, &profile) || profile == 0 ||
			cpuRegs.GPR.n.a0.UL[0] != profile)
			return std::nullopt;
		return CaptureObject(profile);
	}

	bool RestorePayload(const Snapshot& snapshot)
	{
		if (snapshot.data == 0 || snapshot.payload.empty() || snapshot.payload.size() != snapshot.size ||
			!GuestObjects::IsPlausibleAddress(snapshot.data))
			return false;
		return vtlb_memSafeWriteBytes(snapshot.data, snapshot.payload.data(), snapshot.size);
	}
} // namespace AVPE::NativeProfileContract
