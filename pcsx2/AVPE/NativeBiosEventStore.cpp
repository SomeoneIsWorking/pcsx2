// AVP:E bounded BIOS/IOP census event store. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeBiosEventStore.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace AVPE::NativeBiosEventStore
{
	namespace
	{
		struct Event
		{
			u64 sequence = 0;
			std::string kind;
			std::string library;
			std::string function;
			std::string operation;
			std::string outcome;
			std::string module;
			std::string name;
			std::string domain;
			u16 ordinal = 0;
			std::array<u32, 4> arguments{};
			s32 first_result = 0;
			s32 last_result = 0;
			s32 minimum_result = 0;
			s32 maximum_result = 0;
			u32 code = 0;
			u32 pc = 0;
			u32 counter = 0;
			u64 first_result_u64 = 0;
			u64 last_result_u64 = 0;
			u64 minimum_result_u64 = 0;
			u64 maximum_result_u64 = 0;
			u64 result_changes = 0;
			u64 count = 0;
			u64 target = 0;
			u64 cycle = 0;
			u64 calls = 0;
			u8 major = 0;
			u8 minor = 0;
			u32 number = 0;
			u32 handler = 0;
			u32 rpc_id = 0;
			u32 stack_pointer = 0;
			bool hle = false;
			bool debug = false;
			bool branch_delay = false;
			bool overflow = false;
			bool delivered = false;
			bool result_valid = false;
			bool result_u64_valid = false;
			bool result_expected = false;
			bool return_expected = true;
		};

		struct PendingEeSyscall
		{
			std::string name;
			u32 stack_pointer = 0;
			u32 resume_pc = 0;
			u64 order = 0;
			u8 number = 0;
			bool result_expected = false;
			bool result_observed = false;
			bool result_u64 = false;
			bool active = false;
		};

		struct PendingIopImport
		{
			std::string library;
			std::string function;
			u32 stack_pointer = 0;
			u32 resume_pc = 0;
			u64 order = 0;
			u16 ordinal = 0;
			bool hle = false;
			bool debug = false;
			bool active = false;
		};

		bool ReturnsToCaller(const EeSyscallDisposition disposition)
		{
			return disposition != EeSyscallDisposition::NonReturning;
		}

		bool ExpectsResult(const EeSyscallDisposition disposition)
		{
			return disposition == EeSyscallDisposition::ReturningResult ||
			       disposition == EeSyscallDisposition::ReturningU64Result ||
			       disposition == EeSyscallDisposition::ReturningUnobservedResult;
		}

		bool ObservesResult(const EeSyscallDisposition disposition)
		{
			return disposition == EeSyscallDisposition::ReturningResult ||
			       disposition == EeSyscallDisposition::ReturningU64Result;
		}

		bool ObservesU64Result(const EeSyscallDisposition disposition)
		{
			return disposition == EeSyscallDisposition::ReturningU64Result;
		}

		std::string_view ResolvedServiceName(const std::string_view name)
		{
			return name.empty() ? std::string_view{"unknown"} : name;
		}

		std::string JsonEscape(const std::string_view value)
		{
			std::string result;
			result.reserve(value.size());
			for (const char character : value)
			{
				switch (character)
				{
					case '\\':
						result += "\\\\";
						break;
					case '"':
						result += "\\\"";
						break;
					case '\n':
						result += "\\n";
						break;
					case '\r':
						result += "\\r";
						break;
					case '\t':
						result += "\\t";
						break;
					default:
						result += character;
						break;
				}
			}
			return result;
		}

		void AppendString(std::string& json, const std::string_view key, const std::string_view value)
		{
			json += ",\"";
			json += key;
			json += "\":\"";
			json += JsonEscape(value);
			json += '"';
		}

		void AppendArguments(std::string& json, const std::string_view key,
			const std::array<u32, 4>& arguments)
		{
			json += ",\"";
			json += key;
			json += "\":[";
			for (u32 i = 0; i < arguments.size(); ++i)
			{
				if (i != 0)
					json += ',';
				json += std::to_string(arguments[i]);
			}
			json += ']';
		}

		void InitializeResult(Event& event, const s32 result, const u64 result_u64,
			const bool result_u64_valid)
		{
			event.first_result = result;
			event.last_result = result;
			event.minimum_result = result;
			event.maximum_result = result;
			event.first_result_u64 = result_u64;
			event.last_result_u64 = result_u64;
			event.minimum_result_u64 = result_u64;
			event.maximum_result_u64 = result_u64;
			event.result_u64_valid = result_u64_valid;
		}

		void UpdateResult(Event& event, const s32 result, const u64 result_u64)
		{
			if (event.result_u64_valid)
			{
				if (event.last_result_u64 != result_u64)
					++event.result_changes;
				event.last_result_u64 = result_u64;
				event.minimum_result_u64 = std::min(event.minimum_result_u64, result_u64);
				event.maximum_result_u64 = std::max(event.maximum_result_u64, result_u64);
			}
			else
			{
				if (event.last_result != result)
					++event.result_changes;
				event.last_result = result;
				event.minimum_result = std::min(event.minimum_result, result);
				event.maximum_result = std::max(event.maximum_result, result);
			}
		}

		void AppendResultSummary(std::string& json, const Event& event)
		{
			json += ",\"result_summary\":{\"encoding\":\"";
			json += event.result_u64_valid ? "u64" : "s32";
			json += "\",\"first\":";
			if (event.result_u64_valid)
			{
				json += std::to_string(event.first_result_u64);
				json += ",\"last\":" + std::to_string(event.last_result_u64);
				json += ",\"min\":" + std::to_string(event.minimum_result_u64);
				json += ",\"max\":" + std::to_string(event.maximum_result_u64);
			}
			else
			{
				json += std::to_string(event.first_result);
				json += ",\"last\":" + std::to_string(event.last_result);
				json += ",\"min\":" + std::to_string(event.minimum_result);
				json += ",\"max\":" + std::to_string(event.maximum_result);
			}
			json += ",\"changes\":" + std::to_string(event.result_changes) + '}';
		}

		void AppendEvent(std::string& json, const Event& event)
		{
			json += "{\"sequence\":" + std::to_string(event.sequence);
			AppendString(json, "kind", event.kind);
			if (event.kind == "import")
			{
				AppendString(json, "library", event.library);
				json += ",\"ordinal\":" + std::to_string(event.ordinal);
				AppendString(json, "function", event.function);
				AppendArguments(json, "first_arguments", event.arguments);
				AppendString(json, "outcome", event.outcome);
				json += ",\"result_valid\":" + std::string(event.result_valid ? "true" : "false");
				if (event.result_valid)
					AppendResultSummary(json, event);
				else
				{
					json += ",\"first_stack_pointer\":" + std::to_string(event.stack_pointer);
					json += ",\"first_resume_pc\":" + std::to_string(event.pc);
				}
				json += ",\"hle_available\":" + std::string(event.hle ? "true" : "false");
				json += ",\"debug_available\":" + std::string(event.debug ? "true" : "false");
				json += ",\"calls\":" + std::to_string(event.calls);
			}
			else if (event.kind == "ee_syscall")
			{
				json += ",\"number\":" + std::to_string(event.number);
				AppendString(json, "name", event.name);
				AppendArguments(json, "first_arguments", event.arguments);
				AppendString(json, "outcome", event.outcome);
				json += ",\"result_valid\":" + std::string(event.result_valid ? "true" : "false");
				if (event.result_valid)
					AppendResultSummary(json, event);
				json += ",\"result_expected\":" +
				        std::string(event.result_expected ? "true" : "false");
				json += ",\"return_expected\":" +
				        std::string(event.return_expected ? "true" : "false");
				json += ",\"calls\":" + std::to_string(event.calls);
			}
			else if (event.kind == "ee_syscall_return")
			{
				json += ",\"number\":" + std::to_string(event.number);
				AppendString(json, "name", event.name);
				json += ",\"result_expected\":" +
				        std::string(event.result_expected ? "true" : "false");
				json += ",\"result_valid\":" + std::string(event.result_valid ? "true" : "false");
				if (event.result_valid)
					AppendResultSummary(json, event);
				json += ",\"first_stack_pointer\":" + std::to_string(event.stack_pointer);
				json += ",\"first_resume_pc\":" + std::to_string(event.pc);
				json += ",\"calls\":" + std::to_string(event.calls);
			}
			else if (event.kind == "iop_import_return")
			{
				AppendString(json, "library", event.library);
				json += ",\"ordinal\":" + std::to_string(event.ordinal);
				AppendString(json, "function", event.function);
				json += ",\"result_valid\":true";
				AppendResultSummary(json, event);
				json += ",\"hle_available\":" + std::string(event.hle ? "true" : "false");
				json += ",\"debug_available\":" + std::string(event.debug ? "true" : "false");
				json += ",\"first_stack_pointer\":" + std::to_string(event.stack_pointer);
				json += ",\"first_resume_pc\":" + std::to_string(event.pc);
				json += ",\"calls\":" + std::to_string(event.calls);
			}
			else if (event.kind == "exception")
			{
				AppendString(json, "domain", event.domain);
				json += ",\"code\":" + std::to_string(event.code);
				json += ",\"pc\":" + std::to_string(event.pc);
				json += ",\"branch_delay\":" + std::string(event.branch_delay ? "true" : "false");
				json += ",\"calls\":" + std::to_string(event.calls);
			}
			else if (event.kind == "timer")
			{
				AppendString(json, "domain", event.domain);
				json += ",\"counter\":" + std::to_string(event.counter);
				json += ",\"overflow\":" + std::string(event.overflow ? "true" : "false");
				json += ",\"count\":" + std::to_string(event.count);
				json += ",\"target\":" + std::to_string(event.target);
				json += ",\"cycle\":" + std::to_string(event.cycle);
				json += ",\"delivered\":" + std::string(event.delivered ? "true" : "false");
				json += ",\"calls\":" + std::to_string(event.calls);
			}
			else if (event.kind == "module")
			{
				AppendString(json, "operation", event.operation);
				AppendString(json, "module", event.module);
				json += ",\"version_major\":" + std::to_string(event.major);
				json += ",\"version_minor\":" + std::to_string(event.minor);
			}
			else if (event.kind == "interrupt")
			{
				json += ",\"number\":" + std::to_string(event.number);
				AppendString(json, "name", event.name);
				json += ",\"handler\":" + std::to_string(event.handler);
			}
			else if (event.kind == "rpc")
			{
				json += ",\"rpc_id\":" + std::to_string(event.rpc_id);
			}
			json += '}';
		}
	} // namespace

	class Store::Impl
	{
	public:
		explicit Impl(const u32 capacity)
			: capacity(capacity)
		{
		}

		Event* AddEvent()
		{
			if (events.size() >= capacity)
			{
				++overflow;
				return nullptr;
			}
			Event& event = events.emplace_back();
			event.sequence = ++next_sequence;
			return &event;
		}

		void Reset()
		{
			events.clear();
			pending_ee_syscalls = {};
			pending_iop_imports = {};
			next_sequence = 0;
			overflow = 0;
			ee_syscall_pairing_entries = 0;
			ee_syscall_pairing_returns = 0;
			ee_syscall_pairing_sequence_errors = 0;
			ee_syscall_pairing_overflow = 0;
			iop_import_pairing_entries = 0;
			iop_import_pairing_returns = 0;
			iop_import_pairing_overflow = 0;
			next_ee_syscall_order = 0;
			next_iop_import_order = 0;
		}

		const u32 capacity;
		std::vector<Event> events;
		std::array<PendingEeSyscall, 256> pending_ee_syscalls{};
		std::array<PendingIopImport, 256> pending_iop_imports{};
		u64 next_sequence = 0;
		u64 overflow = 0;
		u64 ee_syscall_pairing_entries = 0;
		u64 ee_syscall_pairing_returns = 0;
		u32 ee_syscall_pairing_sequence_errors = 0;
		u32 ee_syscall_pairing_overflow = 0;
		u64 iop_import_pairing_entries = 0;
		u64 iop_import_pairing_returns = 0;
		u32 iop_import_pairing_overflow = 0;
		u64 next_ee_syscall_order = 0;
		u64 next_iop_import_order = 0;
	};

	Store::Store(const u32 capacity)
		: m_impl(std::make_unique<Impl>(capacity))
	{
	}

	Store::~Store() = default;

	void Store::Reset()
	{
		m_impl->Reset();
	}

	void Store::RecordImport(const std::string_view library, const u16 ordinal,
		const std::string_view function, const u32 a0, const u32 a1, const u32 a2, const u32 a3,
		const s32 result, const bool hle, const bool debug, const bool handled,
		const u32 stack_pointer, const u32 resume_pc)
	{
		const std::string_view service_name = ResolvedServiceName(function);
		for (Event& event : m_impl->events)
		{
			if (event.kind == "import" && event.library == library && event.ordinal == ordinal &&
				event.function == service_name && event.hle == hle && event.debug == debug &&
				event.result_valid == handled &&
				(handled || (event.stack_pointer == stack_pointer && event.pc == resume_pc)))
			{
				if (handled)
					UpdateResult(event, result, 0);
				++event.calls;
				return;
			}
		}
		Event* event = m_impl->AddEvent();
		if (!event)
			return;
		event->kind = "import";
		event->library = library;
		event->ordinal = ordinal;
		event->function = service_name;
		event->arguments = {a0, a1, a2, a3};
		event->result_valid = handled;
		if (handled)
			InitializeResult(*event, result, 0, false);
		event->outcome = handled ? "hle" : "oracle";
		event->hle = hle;
		event->debug = debug;
		event->stack_pointer = stack_pointer;
		event->pc = resume_pc;
		event->calls = 1;
	}

	void Store::RecordHandledIopImport(const std::string_view library, const u16 ordinal,
		const std::string_view function, const u32 a0, const u32 a1, const u32 a2,
		const u32 a3, const s32 result, const bool hle, const bool debug)
	{
		RecordImport(library, ordinal, function, a0, a1, a2, a3, result, hle, debug, true, 0, 0);
	}

	bool Store::RecordIopOracleImportEntry(const std::string_view library, const u16 ordinal,
		const std::string_view function, const u32 a0, const u32 a1, const u32 a2,
		const u32 a3, const bool hle, const bool debug, const u32 stack_pointer,
		const u32 resume_pc, const bool return_site_available)
	{
		const std::string_view service_name = ResolvedServiceName(function);
		RecordImport(library, ordinal, function, a0, a1, a2, a3, 0, hle, debug, false,
			stack_pointer, resume_pc);
		++m_impl->iop_import_pairing_entries;
		if (!return_site_available)
		{
			++m_impl->iop_import_pairing_overflow;
			return false;
		}
		auto pending = std::find_if(m_impl->pending_iop_imports.begin(),
			m_impl->pending_iop_imports.end(), [](const PendingIopImport& call) {
				return !call.active;
			});
		if (pending == m_impl->pending_iop_imports.end())
		{
			++m_impl->iop_import_pairing_overflow;
			return false;
		}
		*pending = PendingIopImport{
			.library = std::string(library),
			.function = std::string(service_name),
			.stack_pointer = stack_pointer,
			.resume_pc = resume_pc,
			.order = ++m_impl->next_iop_import_order,
			.ordinal = ordinal,
			.hle = hle,
			.debug = debug,
			.active = true,
		};
		return true;
	}

	bool Store::RecordIopOracleImportReturn(const u32 stack_pointer, const u32 resume_pc,
		const s32 result)
	{
		PendingIopImport* pending = nullptr;
		for (PendingIopImport& candidate : m_impl->pending_iop_imports)
		{
			if (candidate.active && candidate.stack_pointer == stack_pointer &&
				candidate.resume_pc == resume_pc && (!pending || candidate.order > pending->order))
			{
				pending = &candidate;
			}
		}
		if (!pending)
			return false;

		for (Event& event : m_impl->events)
		{
			if (event.kind == "iop_import_return" && event.library == pending->library &&
				event.ordinal == pending->ordinal && event.function == pending->function &&
				event.hle == pending->hle && event.debug == pending->debug &&
				event.stack_pointer == stack_pointer && event.pc == resume_pc)
			{
				UpdateResult(event, result, 0);
				++event.calls;
				++m_impl->iop_import_pairing_returns;
				*pending = {};
				return true;
			}
		}
		if (Event* event = m_impl->AddEvent())
		{
			event->kind = "iop_import_return";
			event->library = pending->library;
			event->ordinal = pending->ordinal;
			event->function = pending->function;
			event->result_valid = true;
			InitializeResult(*event, result, 0, false);
			event->hle = pending->hle;
			event->debug = pending->debug;
			event->stack_pointer = stack_pointer;
			event->pc = resume_pc;
			event->calls = 1;
		}
		++m_impl->iop_import_pairing_returns;
		*pending = {};
		return true;
	}

	void Store::RecordEeSyscall(const u8 number, const std::string_view name,
		const u32 a0, const u32 a1, const u32 a2, const u32 a3, const s32 result,
		const u64 result_u64, const EeSyscallOutcome outcome, const EeSyscallDisposition disposition)
	{
		const std::string_view outcome_name = outcome == EeSyscallOutcome::Bios ? "bios" : "direct";
		const bool result_valid = outcome == EeSyscallOutcome::DirectResult && ObservesResult(disposition);
		const bool result_u64_valid = result_valid && ObservesU64Result(disposition);
		const bool result_expected = ExpectsResult(disposition);
		const bool return_expected = ReturnsToCaller(disposition);
		for (Event& event : m_impl->events)
		{
			if (event.kind == "ee_syscall" && event.number == number && event.name == name &&
				event.outcome == outcome_name && event.result_valid == result_valid &&
				event.result_u64_valid == result_u64_valid &&
				event.result_expected == result_expected &&
				event.return_expected == return_expected)
			{
				if (result_valid)
					UpdateResult(event, result, result_u64);
				++event.calls;
				return;
			}
		}
		Event* event = m_impl->AddEvent();
		if (!event)
			return;
		event->kind = "ee_syscall";
		event->number = number;
		event->name = name;
		event->arguments = {a0, a1, a2, a3};
		event->result_valid = result_valid;
		if (result_valid)
			InitializeResult(*event, result, result_u64, result_u64_valid);
		event->result_expected = result_expected;
		event->outcome = outcome_name;
		event->return_expected = return_expected;
		event->calls = 1;
	}

	void Store::RecordEeBiosSyscallEntry(const u8 number, const std::string_view name,
		const u32 a0, const u32 a1, const u32 a2, const u32 a3,
		const u32 stack_pointer, const u32 resume_pc, const EeSyscallDisposition disposition)
	{
		RecordEeSyscall(number, name, a0, a1, a2, a3, 0, 0, EeSyscallOutcome::Bios, disposition);
		const bool return_expected = ReturnsToCaller(disposition);
		if (!return_expected)
			return;
		++m_impl->ee_syscall_pairing_entries;
		auto pending = std::find_if(m_impl->pending_ee_syscalls.begin(),
			m_impl->pending_ee_syscalls.end(), [](const PendingEeSyscall& call) {
				return !call.active;
			});
		if (pending == m_impl->pending_ee_syscalls.end())
		{
			++m_impl->ee_syscall_pairing_overflow;
			return;
		}
		*pending = PendingEeSyscall{
			.name = std::string(name),
			.stack_pointer = stack_pointer,
			.resume_pc = resume_pc,
			.order = ++m_impl->next_ee_syscall_order,
			.number = number,
			.result_expected = ExpectsResult(disposition),
			.result_observed = ObservesResult(disposition),
			.result_u64 = ObservesU64Result(disposition),
			.active = true,
		};
	}

	void Store::RecordEeBiosSyscallReturn(const u32 stack_pointer, const u32 resume_pc,
		const s32 result, const u64 result_u64)
	{
		PendingEeSyscall* pending = nullptr;
		bool stack_pointer_matched = false;
		for (PendingEeSyscall& candidate : m_impl->pending_ee_syscalls)
		{
			if (!candidate.active || candidate.stack_pointer != stack_pointer)
				continue;
			stack_pointer_matched = true;
			if (candidate.resume_pc == resume_pc && (!pending || candidate.order > pending->order))
				pending = &candidate;
		}
		if (!pending)
		{
			if (stack_pointer_matched)
				++m_impl->ee_syscall_pairing_sequence_errors;
			return;
		}

		for (Event& event : m_impl->events)
		{
			if (event.kind == "ee_syscall_return" && event.number == pending->number &&
				event.name == pending->name && event.result_expected == pending->result_expected &&
				event.result_valid == pending->result_observed &&
				event.result_u64_valid == pending->result_u64)
			{
				if (event.result_valid)
					UpdateResult(event, result, result_u64);
				++event.calls;
				++m_impl->ee_syscall_pairing_returns;
				*pending = {};
				return;
			}
		}
		if (Event* event = m_impl->AddEvent())
		{
			event->kind = "ee_syscall_return";
			event->number = pending->number;
			event->name = pending->name;
			event->result_expected = pending->result_expected;
			event->result_valid = pending->result_observed;
			if (event->result_valid)
				InitializeResult(*event, result, result_u64, pending->result_u64);
			event->stack_pointer = stack_pointer;
			event->pc = resume_pc;
			event->calls = 1;
		}
		++m_impl->ee_syscall_pairing_returns;
		*pending = {};
	}

	void Store::RecordException(const std::string_view domain, const u32 code, const u32 pc,
		const bool branch_delay)
	{
		for (Event& event : m_impl->events)
		{
			if (event.kind == "exception" && event.domain == domain && event.code == code &&
				event.pc == pc && event.branch_delay == branch_delay)
			{
				++event.calls;
				return;
			}
		}
		Event* event = m_impl->AddEvent();
		if (!event)
			return;
		event->kind = "exception";
		event->domain = domain;
		event->code = code;
		event->pc = pc;
		event->branch_delay = branch_delay;
		event->calls = 1;
	}

	void Store::RecordTimer(const std::string_view domain, const u32 index, const bool overflow,
		const u64 count, const u64 target, const u64 cycle, const bool delivered)
	{
		for (Event& event : m_impl->events)
		{
			if (event.kind == "timer" && event.domain == domain && event.counter == index &&
				event.overflow == overflow && event.delivered == delivered)
			{
				++event.calls;
				return;
			}
		}
		Event* event = m_impl->AddEvent();
		if (!event)
			return;
		event->kind = "timer";
		event->domain = domain;
		event->counter = index;
		event->overflow = overflow;
		event->count = count;
		event->target = target;
		event->cycle = cycle;
		event->delivered = delivered;
		event->calls = 1;
	}

	void Store::RecordModule(const std::string_view module, const u8 major, const u8 minor,
		const std::string_view operation)
	{
		Event* event = m_impl->AddEvent();
		if (!event)
			return;
		event->kind = "module";
		event->operation = operation;
		event->module = module;
		event->major = major;
		event->minor = minor;
	}

	void Store::RecordInterrupt(const u32 number, const std::string_view name, const u32 handler)
	{
		Event* event = m_impl->AddEvent();
		if (!event)
			return;
		event->kind = "interrupt";
		event->number = number;
		event->name = name;
		event->handler = handler;
	}

	void Store::RecordRpc(const u32 rpc_id)
	{
		Event* event = m_impl->AddEvent();
		if (!event)
			return;
		event->kind = "rpc";
		event->rpc_id = rpc_id;
	}

	void Store::AppendSnapshotFields(std::string& json) const
	{
		json += ",\"capacity\":" + std::to_string(m_impl->capacity);
		json += ",\"overflow\":" + std::to_string(m_impl->overflow);
		json += ",\"events\":[";
		for (size_t i = 0; i < m_impl->events.size(); ++i)
		{
			if (i != 0)
				json += ',';
			AppendEvent(json, m_impl->events[i]);
		}
		json += "],\"ee_syscall_pairing\":{\"entries\":" +
		        std::to_string(m_impl->ee_syscall_pairing_entries);
		json += ",\"returns\":" + std::to_string(m_impl->ee_syscall_pairing_returns);
		const u64 pending = std::count_if(m_impl->pending_ee_syscalls.begin(),
			m_impl->pending_ee_syscalls.end(), [](const PendingEeSyscall& call) {
				return call.active;
			});
		json += ",\"pending\":" + std::to_string(pending);
		json += ",\"sequence_errors\":" +
		        std::to_string(m_impl->ee_syscall_pairing_sequence_errors);
		json += ",\"overflow\":" + std::to_string(m_impl->ee_syscall_pairing_overflow) + '}';
		json += ",\"iop_import_pairing\":{\"entries\":" +
		        std::to_string(m_impl->iop_import_pairing_entries);
		json += ",\"returns\":" + std::to_string(m_impl->iop_import_pairing_returns);
		const u64 iop_pending = std::count_if(m_impl->pending_iop_imports.begin(),
			m_impl->pending_iop_imports.end(), [](const PendingIopImport& call) {
				return call.active;
			});
		json += ",\"pending\":" + std::to_string(iop_pending);
		json += ",\"overflow\":" + std::to_string(m_impl->iop_import_pairing_overflow) + '}';
	}
} // namespace AVPE::NativeBiosEventStore
