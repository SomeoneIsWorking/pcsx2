// AVP:E native input callback-dispatch evidence. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeInputDispatch.h"

#include "AVPE/AVPE.h"
#include "AVPE/GuestObjects.h"
#include "R5900.h"
#include "VMManager.h"

#include <array>
#include <atomic>
#include <string>
#include <string_view>

namespace AVPE::NativeInputDispatch
{
	namespace
	{
		constexpr std::string_view kSchema = "avpe-input-dispatch-v1";
		constexpr std::string_view kTargetSerial = "SLUS-20147";
		constexpr u32 kTargetCrc = 0x64DA78A3;
		constexpr u32 kCallbackDispatchPc = 0x001147CC;
		constexpr u32 kMemberFunctionWords = 3;
		constexpr u32 kInputDataWords = 3;
		constexpr u32 kMaxCallbackRecords = 32;

		struct CallbackRecord
		{
			std::atomic<u64> dispatches{0};
			std::atomic<u32> owner{0};
			std::atomic<u32> input_data{0};
			std::atomic<u32> input_definition{0};
			std::array<std::atomic<u32>, kMemberFunctionWords> member{};
			std::array<std::atomic<u32>, kInputDataWords> input{};
		};

		struct DispatchSnapshot
		{
			std::atomic<u64> observed{0};
			std::atomic<u64> accepted{0};
			std::atomic<u64> decoded{0};
			std::atomic<u64> truncated{0};
			std::atomic<u32> record_count{0};
			std::array<CallbackRecord, kMaxCallbackRecords> records{};
		};

		DispatchSnapshot s_snapshot;

		bool IsTargetRecognized()
		{
			return VMManager::GetDiscSerial() == kTargetSerial && VMManager::GetDiscCRC() == kTargetCrc;
		}

		bool IsEnabled()
		{
			return IsSurfacelessControlTest() && IsTargetRecognized();
		}

		bool ReadWords(const u32 address, std::array<u32, kMemberFunctionWords>* words)
		{
			for (u32 index = 0; index < words->size(); ++index)
			{
				if (!GuestObjects::ReadWord(address + index * sizeof(u32), &(*words)[index]))
					return false;
			}
			return true;
		}

		void AppendWord(std::string& body, const u32 value)
		{
			constexpr char kHex[] = "0123456789abcdef";
			body += '"';
			body += "0x";
			for (u32 shift = 28;; shift -= 4)
			{
				body += kHex[(value >> shift) & 0x0f];
				if (shift == 0)
					break;
			}
			body += '"';
		}

		void AppendWords(std::string& body, const std::array<std::atomic<u32>, kMemberFunctionWords>& words)
		{
			body += '[';
			for (u32 index = 0; index < words.size(); ++index)
			{
				if (index != 0)
					body += ',';
				AppendWord(body, words[index].load(std::memory_order_acquire));
			}
			body += ']';
		}

		bool Matches(const CallbackRecord& record, const u32 owner,
			const std::array<u32, kMemberFunctionWords>& member)
		{
			if (record.owner.load(std::memory_order_acquire) != owner)
				return false;
			for (u32 index = 0; index < member.size(); ++index)
			{
				if (record.member[index].load(std::memory_order_acquire) != member[index])
					return false;
			}
			return true;
		}

		CallbackRecord* FindOrAddRecord(const u32 owner, const std::array<u32, kMemberFunctionWords>& member)
		{
			const u32 count = s_snapshot.record_count.load(std::memory_order_acquire);
			for (u32 index = 0; index < count; ++index)
			{
				CallbackRecord& candidate = s_snapshot.records[index];
				if (Matches(candidate, owner, member))
					return &candidate;
			}
			if (count == s_snapshot.records.size())
				return nullptr;

			CallbackRecord& record = s_snapshot.records[count];
			record.owner.store(owner, std::memory_order_relaxed);
			for (u32 index = 0; index < member.size(); ++index)
				record.member[index].store(member[index], std::memory_order_relaxed);
			s_snapshot.record_count.store(count + 1, std::memory_order_release);
			return &record;
		}

		void AppendRecord(std::string& body, const CallbackRecord& record)
		{
			body += "{\"dispatches\":" +
			        std::to_string(record.dispatches.load(std::memory_order_acquire));
			body += ",\"owner\":";
			AppendWord(body, record.owner.load(std::memory_order_acquire));
			body += ",\"input_data\":";
			AppendWord(body, record.input_data.load(std::memory_order_acquire));
			body += ",\"input_definition\":";
			AppendWord(body, record.input_definition.load(std::memory_order_acquire));
			body += ",\"member_function\":";
			AppendWords(body, record.member);
			body += ",\"input_words\":";
			AppendWords(body, record.input);
			body += '}';
		}
	} // namespace

	bool ShouldInstrumentEePc(const u32 pc)
	{
		return pc == kCallbackDispatchPc;
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (pc != kCallbackDispatchPc)
			return;

		s_snapshot.observed.fetch_add(1, std::memory_order_relaxed);
		if (!IsEnabled())
			return;
		s_snapshot.accepted.fetch_add(1, std::memory_order_release);

		const u32 member = cpuRegs.GPR.n.t9.UL[0];
		std::array<u32, kMemberFunctionWords> member_words{};
		std::array<u32, kInputDataWords> input_words{};
		const u32 input_data = cpuRegs.GPR.n.a1.UL[0];
		if (!ReadWords(member, &member_words) || !ReadWords(input_data, &input_words))
			return;
		s_snapshot.decoded.fetch_add(1, std::memory_order_release);

		CallbackRecord* const record = FindOrAddRecord(cpuRegs.GPR.n.a0.UL[0], member_words);
		if (!record)
		{
			s_snapshot.truncated.fetch_add(1, std::memory_order_release);
			return;
		}

		record->input_data.store(input_data, std::memory_order_relaxed);
		record->input_definition.store(cpuRegs.GPR.n.a2.UL[0], std::memory_order_relaxed);
		for (u32 index = 0; index < input_words.size(); ++index)
			record->input[index].store(input_words[index], std::memory_order_relaxed);
		record->dispatches.fetch_add(1, std::memory_order_release);
	}

	std::string SnapshotJson()
	{
		const u64 observed = s_snapshot.observed.load(std::memory_order_acquire);
		const u64 accepted = s_snapshot.accepted.load(std::memory_order_acquire);
		const u64 decoded = s_snapshot.decoded.load(std::memory_order_acquire);
		const u64 truncated = s_snapshot.truncated.load(std::memory_order_acquire);
		const u32 record_count = s_snapshot.record_count.load(std::memory_order_acquire);
		std::string body = "{\"schema\":\"" + std::string(kSchema) + "\",\"dispatch_pc\":";
		AppendWord(body, kCallbackDispatchPc);
		body += ",\"observed_dispatches\":" + std::to_string(observed);
		body += ",\"accepted_dispatches\":" + std::to_string(accepted);
		body += ",\"rejected_dispatches\":" + std::to_string(observed - accepted);
		body += ",\"decoded_dispatches\":" + std::to_string(decoded);
		body += ",\"truncated_dispatches\":" + std::to_string(truncated);
		body += ",\"callbacks\":[";
		for (u32 index = 0; index < record_count; ++index)
		{
			if (index != 0)
				body += ',';
			AppendRecord(body, s_snapshot.records[index]);
		}
		body += ']';
		body += '}';
		return body;
	}

	void Reset()
	{
		s_snapshot.observed.store(0, std::memory_order_release);
		s_snapshot.accepted.store(0, std::memory_order_release);
		s_snapshot.decoded.store(0, std::memory_order_release);
		s_snapshot.truncated.store(0, std::memory_order_release);
		for (CallbackRecord& record : s_snapshot.records)
		{
			record.dispatches.store(0, std::memory_order_release);
			record.owner.store(0, std::memory_order_release);
			record.input_data.store(0, std::memory_order_release);
			record.input_definition.store(0, std::memory_order_release);
			for (std::atomic<u32>& value : record.member)
				value.store(0, std::memory_order_release);
			for (std::atomic<u32>& value : record.input)
				value.store(0, std::memory_order_release);
		}
		s_snapshot.record_count.store(0, std::memory_order_release);
	}
} // namespace AVPE::NativeInputDispatch
