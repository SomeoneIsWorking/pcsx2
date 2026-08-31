// AVP:E BIOS/IOP observation trace. Fork-local; not for upstream PCSX2.

#pragma once

#include "AVPE/NativeBiosEventStore.h"
#include "common/Pcsx2Defs.h"

#include <chrono>
#include <string>
#include <string_view>

namespace AVPE::NativeBiosTrace
{
	constexpr u32 MaximumEvents = 4096;
	using EeSyscallDisposition = NativeBiosEventStore::EeSyscallDisposition;
	using EeSyscallOutcome = NativeBiosEventStore::EeSyscallOutcome;

	void Reset();
	void SetEnabled(bool enabled);
	bool IsEnabled();

	void RecordHandledIopImport(std::string_view library, u16 ordinal,
		std::string_view function, u32 a0, u32 a1, u32 a2, u32 a3, s32 result,
		bool hle, bool debug);
	bool RecordIopOracleImportEntry(std::string_view library, u16 ordinal,
		std::string_view function, u32 a0, u32 a1, u32 a2, u32 a3, bool hle,
		bool debug, u32 stack_pointer, u32 resume_pc);
	void RecordIopOracleImportReturn(u32 stack_pointer, u32 resume_pc, s32 result);
	bool ShouldObserveIopImportReturn(u32 pc);
	void RecordEeSyscall(u8 number, std::string_view name, u32 a0, u32 a1, u32 a2, u32 a3,
		s32 result, u64 result_u64, EeSyscallOutcome outcome, EeSyscallDisposition disposition);
	void RecordEeBiosSyscallEntry(u8 number, std::string_view name, u32 a0, u32 a1, u32 a2,
		u32 a3, u32 stack_pointer, u32 resume_pc, EeSyscallDisposition disposition);
	void RecordEeBiosSyscallReturn(u32 stack_pointer, u32 resume_pc, s32 result, u64 result_u64);
	void RecordCurrentEeSyscall(u8 number, EeSyscallOutcome outcome);
	bool ShouldInstrumentEeSyscallReturn(u32 pc);
	void ObserveEeSyscallReturn(u32 pc);
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
	void ObserveMissionLoadError(u32 pc, u32 argument, u32 return_pc);
	void ObserveMissionLoadProgress(u32 pc, u32 chunk_size, u32 callback_pc,
		u32 stack_remaining, bool stack_remaining_valid);
	void ObserveMissionPostReadProgress(u32 pc, u32 chunk_descriptor);
	void ObserveMissionTypeInitializer(u32 pc, u32 target, u32 object, u32 descriptor,
		u32 remaining, u32 stack_pointer, u32 symbol, u32 metadata, bool descriptor_valid);
	void ObserveMissionObjectFactory(u32 pc, u32 target, u32 class_entry, u32 handle,
		u32 fill_data, u32 stack_pointer);
	// A structured incomplete result identifies a missing grounded return; an
	// empty result means no mission phase was active.
	std::string CaptureMissionBoundaryJson(std::chrono::milliseconds timeout);
} // namespace AVPE::NativeBiosTrace
