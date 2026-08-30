// AVP:E bounded BIOS/IOP census event store. Fork-local; not for upstream PCSX2.

#pragma once

#include "common/Pcsx2Defs.h"

#include <memory>
#include <string>
#include <string_view>

namespace AVPE::NativeBiosEventStore
{
	enum class EeSyscallOutcome : u8
	{
		Bios,
		DirectNoResult,
		DirectResult,
	};

	enum class EeSyscallDisposition : u8
	{
		ReturningResult,
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
		void RecordImport(std::string_view library, u16 ordinal, std::string_view function,
			u32 a0, u32 a1, u32 a2, u32 a3, s32 result, bool hle, bool debug, bool handled);
		void RecordEeSyscall(u8 number, std::string_view name, u32 a0, u32 a1, u32 a2, u32 a3,
			s32 result, EeSyscallOutcome outcome, EeSyscallDisposition disposition);
		void RecordEeBiosSyscallEntry(u8 number, std::string_view name, u32 a0, u32 a1, u32 a2,
			u32 a3, u32 stack_pointer, u32 resume_pc, EeSyscallDisposition disposition);
		void RecordEeBiosSyscallReturn(u32 stack_pointer, u32 resume_pc, s32 result);
		void RecordException(std::string_view domain, u32 code, u32 pc, bool branch_delay);
		void RecordTimer(std::string_view domain, u32 index, bool overflow, u64 count, u64 target,
			u64 cycle, bool delivered);
		void RecordModule(std::string_view module, u8 major, u8 minor, std::string_view operation);
		void RecordInterrupt(u32 number, std::string_view name, u32 handler);
		void RecordRpc(u32 rpc_id);

		// Append capacity, events, and syscall-pairing fields to an open JSON object.
		void AppendSnapshotFields(std::string& json) const;

	private:
		class Impl;
		std::unique_ptr<Impl> m_impl;
	};
} // namespace AVPE::NativeBiosEventStore
