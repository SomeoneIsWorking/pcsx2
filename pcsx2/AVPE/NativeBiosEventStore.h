// AVP:E bounded BIOS/IOP census event store. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <memory>
#include <string>
#include <string_view>

namespace AVPE::NativeBiosEventStore
{
	struct ExceptionTransition
	{
		u32 status_before = 0;
		u32 status_after = 0;
		u32 cause_after = 0;
		u32 epc_after = 0;
		u32 vector_pc = 0;
		bool operator==(const ExceptionTransition&) const = default;
	};

	enum class EeSyscallOutcome : u8
	{
		Bios,
		DirectNoResult,
		DirectResult,
	};

	enum class EeSyscallDisposition : u8
	{
		ReturningResult,
		ReturningU64Result,
		ReturningNoResult,
		ReturningUnobservedResult,
		NonReturning,
	};

	class Store
	{
	public:
		explicit Store(u32 capacity);
		~Store();

		Store(const Store&) = delete;
		Store& operator=(const Store&) = delete;

		void Reset();
		void RecordHandledIopImport(std::string_view library, u16 ordinal,
			std::string_view function, u32 a0, u32 a1, u32 a2, u32 a3, s32 result,
			bool hle, bool debug);
		bool RecordIopOracleImportEntry(std::string_view library, u16 ordinal,
			std::string_view function, u32 a0, u32 a1, u32 a2, u32 a3, bool hle,
			bool debug, u32 stack_pointer, u32 resume_pc, bool return_site_available);
		bool RecordIopOracleImportReturn(u32 stack_pointer, u32 resume_pc, s32 result);
		void RecordEeSyscall(u8 number, std::string_view name, u32 a0, u32 a1, u32 a2, u32 a3,
			s32 result, u64 result_u64, EeSyscallOutcome outcome, EeSyscallDisposition disposition);
		void RecordEeBiosSyscallEntry(u8 number, std::string_view name, u32 a0, u32 a1, u32 a2,
			u32 a3, u32 stack_pointer, u32 resume_pc, EeSyscallDisposition disposition);
		void RecordEeBiosSyscallReturn(u32 stack_pointer, u32 resume_pc, s32 result, u64 result_u64);
		void RecordException(std::string_view domain, u32 code, u32 pc, bool branch_delay,
			const ExceptionTransition& transition);
		void RecordTimer(std::string_view domain, u32 index, bool overflow, u64 count, u64 target,
			u64 cycle, bool delivered);
		void RecordModule(std::string_view module, u8 major, u8 minor, std::string_view operation);
		void RecordInterrupt(u32 number, std::string_view name, u32 handler);
		void RecordRpc(u32 rpc_id);

		// Append capacity, events, and syscall-pairing fields to an open JSON object.
		void AppendSnapshotFields(std::string& json) const;

	private:
		void RecordImport(std::string_view library, u16 ordinal, std::string_view function,
			u32 a0, u32 a1, u32 a2, u32 a3, s32 result, bool hle, bool debug, bool handled,
			u32 stack_pointer, u32 resume_pc);

		class Impl;
		std::unique_ptr<Impl> m_impl;
	};
} // namespace AVPE::NativeBiosEventStore
