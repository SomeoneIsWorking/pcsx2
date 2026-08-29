// AVP:E BIOS/IOP observation trace. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <chrono>
#include <string>
#include <string_view>

namespace AVPE::NativeBiosTrace
{
	constexpr u32 MaximumEvents = 4096;

	void Reset();
	void SetEnabled(bool enabled);
	bool IsEnabled();

	void RecordImport(std::string_view library, u16 ordinal, std::string_view function,
		u32 a0, u32 a1, u32 a2, u32 a3, s32 result, bool hle, bool debug);
	void RecordEeSyscall(u8 number, std::string_view name, u32 a0, u32 a1, u32 a2, u32 a3,
		s32 result);
	void RecordException(std::string_view domain, u32 code, u32 pc, bool branch_delay);
	void RecordTimer(std::string_view domain, u32 index, bool overflow, u64 count, u64 target,
		u64 cycle, bool delivered);
	void RecordModule(std::string_view module, u8 major, u8 minor, std::string_view operation);
	void RecordInterrupt(u32 number, std::string_view name, u32 handler);
	void RecordRpc(u32 rpc_id);

	std::string SnapshotJson();
	std::string SnapshotAndDisableJson();
	// Wait for the next guest CPU frame boundary, then atomically capture and disable.
	// An empty result means no boundary arrived before the deadline.
	std::string CaptureAtGuestBoundaryJson(std::chrono::milliseconds timeout);
	void OnGuestFrameBoundary();

	// Arm a one-shot boundary around the grounded Marine M1 ShellLoadLevel call.
	void StartMissionBoundary();
	bool ShouldInstrumentMissionBoundary(u32 pc);
	void ObserveMissionBoundary(u32 pc);
	// An empty result means the grounded return was not observed before the deadline.
	std::string CaptureMissionBoundaryJson(std::chrono::milliseconds timeout);
} // namespace AVPE::NativeBiosTrace
