// AVP:E native input callback-dispatch evidence. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeInputDispatch.h"

#include "AVPE/AVPE.h"
#include "AVPE/GuestObjects.h"
#include "R5900.h"
#include "VMManager.h"
#include "vtlb.h"

#include <array>
#include <atomic>
#include <bit>
#include <string>
#include <string_view>

namespace AVPE::NativeInputDispatch
{
	namespace
	{
		constexpr std::string_view kSchema = "avpe-input-dispatch-v2";
		constexpr std::string_view kTargetSerial = "SLUS-20147";
		constexpr u32 kTargetCrc = 0x64DA78A3;
		constexpr u32 kCallbackDispatchPc = 0x001147CC;
		constexpr u32 kCallbackDispatchReturnPc = 0x001147D8;
		constexpr u32 kMemberFunctionWords = 3;
		constexpr u32 kInputDataWords = 3;
		constexpr u32 kMaxCallbackRecords = 32;
		constexpr u32 kPointerUpdateFunction = 0x001B51A0;

		struct PendingPointerMotion
		{
			std::atomic_bool pending{false};
			std::atomic<u64> next_id{1};
			std::atomic<u64> queued_id{0};
			std::atomic<u64> injected_id{0};
			std::atomic<u64> rejected_id{0};
			std::atomic<u32> pointer{0};
			std::atomic<u32> callback{0};
			std::atomic<u32> x{0};
			std::atomic<u32> y{0};
		};

		struct PendingMenuAction
		{
			std::atomic_bool pending{false};
			std::atomic<u64> next_id{1};
			std::atomic<u64> queued_id{0};
			std::atomic<u64> injected_id{0};
			std::atomic<u64> completed_id{0};
			std::atomic<u64> rejected_id{0};
			std::atomic_bool return_pending{false};
			std::atomic<u32> target{0};
			std::atomic<u32> callback{0};
			std::atomic<u32> function{0};
		};

		struct CallbackRecord
		{
			std::atomic<u64> dispatches{0};
			std::atomic<u32> owner{0};
			std::atomic<u32> owner_vtable{0};
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
		PendingPointerMotion s_pending;
		PendingMenuAction s_pending_menu_action;

		bool IsTargetRecognized()
		{
			return VMManager::GetDiscSerial() == kTargetSerial && VMManager::GetDiscCRC() == kTargetCrc;
		}

		bool IsEnabled()
		{
			return IsSurfacelessControlTest() && IsTargetRecognized();
		}

		bool IsPointerCallbackValid(const u32 pointer, const u32 callback)
		{
			u32 owner_handle = 0;
			u32 owner = 0;
			u32 vtable = 0;
			u32 target = 0;
			std::array<u32, kMemberFunctionWords> member{};
			return GuestObjects::IsPlausibleObject(pointer) && GuestObjects::IsPlausibleAddress(callback) &&
			       GuestObjects::ReadWord(callback + 8, &owner_handle) &&
			       GuestObjects::ResolveHandle(owner_handle, &owner) && owner == pointer &&
			       GuestObjects::ReadBytes(callback + 0x0C, member.data(), sizeof(member)) &&
			       member[0] == 0 && member[1] != 0 && (member[1] & 3) == 0 && member[2] == 0 &&
			       GuestObjects::ReadWord(pointer, &vtable) && GuestObjects::IsPlausibleAddress(vtable) &&
			       GuestObjects::ReadWord(vtable + member[1], &target) && target == kPointerUpdateFunction;
		}

		bool IsMenuCallbackValid(const u32 target, const u32 callback, const u32 function)
		{
			u32 owner_handle = 0;
			u32 owner = 0;
			u32 resolved_function = 0;
			if (!GuestObjects::IsPlausibleObject(target) || !GuestObjects::IsPlausibleAddress(callback) ||
				!GuestObjects::IsPlausibleAddress(function) ||
				!GuestObjects::ReadWord(callback + 8, &owner_handle) ||
				!GuestObjects::ResolveHandle(owner_handle, &owner) || owner != target ||
				!GuestObjects::ResolveMemberFunction(target, callback + 0x0C, &resolved_function))
				return false;
			return resolved_function == function;
		}

		void RejectPendingPointerMotion(const u64 id)
		{
			s_pending.rejected_id.store(id, std::memory_order_release);
			s_pending.pending.store(false, std::memory_order_release);
		}

		void RejectPendingMenuAction(const u64 id)
		{
			s_pending_menu_action.rejected_id.store(id, std::memory_order_release);
			s_pending_menu_action.pending.store(false, std::memory_order_release);
		}

		void CompletePendingMenuAction()
		{
			if (!s_pending_menu_action.return_pending.exchange(false, std::memory_order_acq_rel))
				return;
			const u64 id = s_pending_menu_action.queued_id.load(std::memory_order_acquire);
			s_pending_menu_action.completed_id.store(id, std::memory_order_release);
		}

		void InjectPendingPointerMotion()
		{
			if (!s_pending.pending.load(std::memory_order_acquire))
				return;

			const u64 id = s_pending.queued_id.load(std::memory_order_acquire);
			const u32 pointer = s_pending.pointer.load(std::memory_order_acquire);
			const u32 callback = s_pending.callback.load(std::memory_order_acquire);
			const u32 input_data = cpuRegs.GPR.n.a1.UL[0];
			if (!IsTargetRecognized() || !IsPointerCallbackValid(pointer, callback) || input_data < 12 ||
				!GuestObjects::IsPlausibleAddress(input_data))
			{
				RejectPendingPointerMotion(id);
				return;
			}

			const std::array<u32, kInputDataWords> input = {
				s_pending.x.load(std::memory_order_acquire),
				s_pending.y.load(std::memory_order_acquire),
				0,
			};
			if (!vtlb_memSafeWriteBytes(input_data, input.data(), sizeof(input)))
			{
				RejectPendingPointerMotion(id);
				return;
			}

			cpuRegs.GPR.n.a0.UL[0] = pointer;
			cpuRegs.GPR.n.a2.UL[0] = input_data - 12;
			cpuRegs.GPR.n.t9.UL[0] = callback + 0x0C;
			s_pending.injected_id.store(id, std::memory_order_release);
			s_pending.pending.store(false, std::memory_order_release);
		}

		void InjectPendingMenuAction()
		{
			if (!s_pending_menu_action.pending.load(std::memory_order_acquire))
				return;

			const u64 id = s_pending_menu_action.queued_id.load(std::memory_order_acquire);
			const u32 target = s_pending_menu_action.target.load(std::memory_order_acquire);
			const u32 callback = s_pending_menu_action.callback.load(std::memory_order_acquire);
			const u32 function = s_pending_menu_action.function.load(std::memory_order_acquire);
			if (!IsTargetRecognized() || !IsMenuCallbackValid(target, callback, function))
			{
				RejectPendingMenuAction(id);
				return;
			}

			cpuRegs.GPR.n.a0.UL[0] = target;
			cpuRegs.GPR.n.t9.UL[0] = callback + 0x0C;
			s_pending_menu_action.injected_id.store(id, std::memory_order_release);
			s_pending_menu_action.return_pending.store(true, std::memory_order_release);
			s_pending_menu_action.pending.store(false, std::memory_order_release);
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
			body += ",\"owner_vtable\":";
			AppendWord(body, record.owner_vtable.load(std::memory_order_acquire));
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
		return pc == kCallbackDispatchPc || pc == kCallbackDispatchReturnPc;
	}

	Result QueuePointerMotion(const PointerMotionRequest& request)
	{
		if (s_pending.pending.load(std::memory_order_acquire) ||
			s_pending_menu_action.pending.load(std::memory_order_acquire) ||
			s_pending_menu_action.return_pending.load(std::memory_order_acquire))
			return {.status = Status::Busy, .error = "an input callback is already queued"};
		if (!IsTargetRecognized() || !IsPointerCallbackValid(request.pointer, request.callback))
		{
			return {
				.status = Status::InvalidPointerCallback,
				.error = "pointer callback no longer resolves to AVP:E relative motion",
			};
		}

		const u64 id = s_pending.next_id.fetch_add(1, std::memory_order_relaxed);
		s_pending.pointer.store(request.pointer, std::memory_order_relaxed);
		s_pending.callback.store(request.callback, std::memory_order_relaxed);
		s_pending.x.store(std::bit_cast<u32>(request.x), std::memory_order_relaxed);
		s_pending.y.store(std::bit_cast<u32>(request.y), std::memory_order_relaxed);
		s_pending.queued_id.store(id, std::memory_order_relaxed);
		s_pending.pending.store(true, std::memory_order_release);
		return {.status = Status::Success, .id = id};
	}

	Result QueueMenuAction(const MenuActionRequest& request)
	{
		if (s_pending.pending.load(std::memory_order_acquire) ||
			s_pending_menu_action.pending.load(std::memory_order_acquire) ||
			s_pending_menu_action.return_pending.load(std::memory_order_acquire))
			return {.status = Status::Busy, .error = "an input callback is already queued"};
		if (!IsTargetRecognized() ||
			!IsMenuCallbackValid(request.target, request.callback, request.function))
		{
			return {
				.status = Status::InvalidMenuCallback,
				.error = "menu callback no longer resolves to its exact AVP:E target",
			};
		}

		const u64 id = s_pending_menu_action.next_id.fetch_add(1, std::memory_order_relaxed);
		s_pending_menu_action.target.store(request.target, std::memory_order_relaxed);
		s_pending_menu_action.callback.store(request.callback, std::memory_order_relaxed);
		s_pending_menu_action.function.store(request.function, std::memory_order_relaxed);
		s_pending_menu_action.queued_id.store(id, std::memory_order_relaxed);
		s_pending_menu_action.pending.store(true, std::memory_order_release);
		return {.status = Status::Success, .id = id};
	}

	void ObserveEeExecution(const u32 pc)
	{
		if (pc == kCallbackDispatchReturnPc)
		{
			CompletePendingMenuAction();
			return;
		}
		if (pc != kCallbackDispatchPc)
			return;
		InjectPendingPointerMotion();
		InjectPendingMenuAction();

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
		u32 owner_vtable = 0;
		GuestObjects::ReadWord(cpuRegs.GPR.n.a0.UL[0], &owner_vtable);
		record->owner_vtable.store(owner_vtable, std::memory_order_relaxed);
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
		body += ",\"pending_pointer_id\":" +
		        std::to_string(s_pending.pending.load(std::memory_order_acquire) ?
								   s_pending.queued_id.load(std::memory_order_acquire) :
								   0);
		body += ",\"injected_pointer_id\":" +
		        std::to_string(s_pending.injected_id.load(std::memory_order_acquire));
		body += ",\"rejected_pointer_id\":" +
		        std::to_string(s_pending.rejected_id.load(std::memory_order_acquire));
		body += ",\"pending_menu_action_id\":" +
		        std::to_string(s_pending_menu_action.pending.load(std::memory_order_acquire) ?
								   s_pending_menu_action.queued_id.load(std::memory_order_acquire) :
								   0);
		body += ",\"injected_menu_action_id\":" +
		        std::to_string(s_pending_menu_action.injected_id.load(std::memory_order_acquire));
		body += ",\"completed_menu_action_id\":" +
		        std::to_string(s_pending_menu_action.completed_id.load(std::memory_order_acquire));
		body += ",\"rejected_menu_action_id\":" +
		        std::to_string(s_pending_menu_action.rejected_id.load(std::memory_order_acquire));
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
		s_pending.pending.store(false, std::memory_order_release);
		s_pending.queued_id.store(0, std::memory_order_release);
		s_pending.injected_id.store(0, std::memory_order_release);
		s_pending.rejected_id.store(0, std::memory_order_release);
		s_pending.pointer.store(0, std::memory_order_release);
		s_pending.callback.store(0, std::memory_order_release);
		s_pending.x.store(0, std::memory_order_release);
		s_pending.y.store(0, std::memory_order_release);
		s_pending_menu_action.pending.store(false, std::memory_order_release);
		s_pending_menu_action.queued_id.store(0, std::memory_order_release);
		s_pending_menu_action.injected_id.store(0, std::memory_order_release);
		s_pending_menu_action.completed_id.store(0, std::memory_order_release);
		s_pending_menu_action.rejected_id.store(0, std::memory_order_release);
		s_pending_menu_action.return_pending.store(false, std::memory_order_release);
		s_pending_menu_action.target.store(0, std::memory_order_release);
		s_pending_menu_action.callback.store(0, std::memory_order_release);
		s_pending_menu_action.function.store(0, std::memory_order_release);
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
