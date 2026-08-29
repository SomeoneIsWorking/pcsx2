// AVP:E BIOS/IOP observation trace. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeBiosTrace.h"

#include "AVPE/LoadTimingPoint.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

namespace AVPE::NativeBiosTrace
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
			std::string module;
			std::string name;
			std::string domain;
			u16 ordinal = 0;
			std::array<u32, 4> arguments{};
			s32 result = 0;
			u32 code = 0;
			u32 pc = 0;
			u32 counter = 0;
			u64 count = 0;
			u64 target = 0;
			u64 cycle = 0;
			u64 calls = 0;
			u8 major = 0;
			u8 minor = 0;
			u32 number = 0;
			u32 handler = 0;
			u32 rpc_id = 0;
			bool hle = false;
			bool debug = false;
			bool branch_delay = false;
			bool overflow = false;
			bool delivered = false;
		};

		std::mutex s_mutex;
		std::condition_variable s_capture_condition;
		std::condition_variable s_mission_condition;
		std::vector<Event> s_events;
		u64 s_next_sequence = 0;
		u64 s_overflow = 0;
		bool s_capture_requested = false;
		std::string s_capture_result;
		std::string s_mission_result;
		std::optional<LoadTimingPoint::Point> s_mission_entry;
		std::optional<LoadTimingPoint::Point> s_mission_return;
		u64 s_mission_ordinal = 0;
		u32 s_mission_sequence_errors = 0;

		std::atomic_bool s_enabled{false};
		std::atomic_bool s_mission_armed{false};

		constexpr u32 MissionEntryPc = 0x0016F910;
		constexpr u32 MissionReturnPc = 0x0016FA4C;

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

		template <typename Fill>
		void AddEvent(Fill&& fill)
		{
			if (!s_enabled.load(std::memory_order_acquire))
				return;
			std::lock_guard lock(s_mutex);
			if (!s_enabled.load(std::memory_order_relaxed))
				return;
			if (s_events.size() >= MaximumEvents)
			{
				++s_overflow;
				return;
			}
			Event event;
			event.sequence = ++s_next_sequence;
			fill(event);
			s_events.emplace_back(std::move(event));
		}

		void AppendString(std::string& json, const std::string_view key, const std::string_view value)
		{
			json += ",\"";
			json += key;
			json += "\":\"";
			json += JsonEscape(value);
			json += '"';
		}

		void AppendArguments(std::string& json, const std::array<u32, 4>& arguments)
		{
			json += ",\"arguments\":[";
			for (u32 i = 0; i < arguments.size(); ++i)
			{
				if (i != 0)
					json += ',';
				json += std::to_string(arguments[i]);
			}
			json += ']';
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
				AppendArguments(json, event.arguments);
				json += ",\"result\":" + std::to_string(event.result);
				json += ",\"hle\":" + std::string(event.hle ? "true" : "false");
				json += ",\"debug\":" + std::string(event.debug ? "true" : "false");
				json += ",\"calls\":" + std::to_string(event.calls);
			}
			else if (event.kind == "ee_syscall")
			{
				json += ",\"number\":" + std::to_string(event.number);
				AppendString(json, "name", event.name);
				AppendArguments(json, event.arguments);
				json += ",\"result\":" + std::to_string(event.result);
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

		std::string SnapshotJsonLocked(const bool disable_after_snapshot)
		{
			const bool enabled = s_enabled.load(std::memory_order_relaxed);
			if (disable_after_snapshot)
				s_enabled.store(false, std::memory_order_release);
			std::string json = "{\"schema\":\"avpe-bios-trace-v1\",\"enabled\":";
			json += enabled ? "true" : "false";
			json += ",\"capacity\":" + std::to_string(MaximumEvents);
			json += ",\"overflow\":" + std::to_string(s_overflow);
			json += ",\"events\":[";
			for (size_t i = 0; i < s_events.size(); ++i)
			{
				if (i != 0)
					json += ',';
				AppendEvent(json, s_events[i]);
			}
			json += "]}";
			return json;
		}

		void ResetMissionLocked()
		{
			s_mission_armed.store(false, std::memory_order_release);
			s_mission_result.clear();
			s_mission_entry.reset();
			s_mission_return.reset();
			s_mission_ordinal = 0;
			s_mission_sequence_errors = 0;
		}

		void ResetObservationsLocked()
		{
			s_events.clear();
			s_next_sequence = 0;
			s_overflow = 0;
			s_capture_requested = false;
			s_capture_result.clear();
			ResetMissionLocked();
		}

		void AppendMissionPoint(std::string& json, const LoadTimingPoint::Point& point, const u32 pc)
		{
			json += "{\"pc\":" + std::to_string(pc);
			json += ",\"ordinal\":" + std::to_string(point.ordinal);
			json += ",\"ee_cycle\":" + std::to_string(point.ee_cycle);
			json += ",\"iop_cycle\":" + std::to_string(point.iop_cycle);
			json += ",\"frame\":" + std::to_string(point.frame);
			json += ",\"host_time_ns\":" + std::to_string(point.host_time_ns) + '}';
		}

		std::string BuildMissionResultLocked()
		{
			std::string json = SnapshotJsonLocked(true);
			if (json.empty() || json.back() != '}')
				return {};
			json.pop_back();
			json += ",\"mission_boundary\":{\"entry_pc\":" + std::to_string(MissionEntryPc);
			json += ",\"return_pc\":" + std::to_string(MissionReturnPc);
			json += ",\"complete\":";
			const bool complete = s_mission_entry && s_mission_return && s_mission_sequence_errors == 0 &&
			                      s_mission_return->ee_cycle > s_mission_entry->ee_cycle &&
			                      s_mission_return->iop_cycle > s_mission_entry->iop_cycle;
			json += complete ? "true" : "false";
			json += ",\"sequence_errors\":" + std::to_string(s_mission_sequence_errors);
			json += ",\"entry\":";
			if (s_mission_entry)
				AppendMissionPoint(json, *s_mission_entry, MissionEntryPc);
			else
				json += "null";
			json += ",\"return\":";
			if (s_mission_return)
				AppendMissionPoint(json, *s_mission_return, MissionReturnPc);
			else
				json += "null";
			json += "}}";
			return json;
		}
	} // namespace

	void Reset()
	{
		std::lock_guard lock(s_mutex);
		ResetObservationsLocked();
		s_capture_condition.notify_all();
		s_mission_condition.notify_all();
	}

	void SetEnabled(const bool enabled)
	{
		std::lock_guard lock(s_mutex);
		s_enabled.store(enabled, std::memory_order_release);
		ResetObservationsLocked();
		s_capture_condition.notify_all();
		s_mission_condition.notify_all();
	}

	bool IsEnabled()
	{
		std::lock_guard lock(s_mutex);
		return s_enabled.load(std::memory_order_relaxed);
	}

	void RecordImport(const std::string_view library, const u16 ordinal, const std::string_view function,
		const u32 a0, const u32 a1, const u32 a2, const u32 a3, const s32 result,
		const bool hle, const bool debug)
	{
		if (!s_enabled.load(std::memory_order_acquire))
			return;
		std::lock_guard lock(s_mutex);
		if (!s_enabled.load(std::memory_order_relaxed))
			return;
		for (Event& event : s_events)
		{
			if (event.kind == "import" && event.library == library && event.ordinal == ordinal &&
				event.function == function && event.hle == hle && event.debug == debug)
			{
				++event.calls;
				return;
			}
		}
		if (s_events.size() >= MaximumEvents)
		{
			++s_overflow;
			return;
		}
		Event event;
		event.sequence = ++s_next_sequence;
		event.kind = "import";
		event.library = library;
		event.ordinal = ordinal;
		event.function = function;
		event.arguments = {a0, a1, a2, a3};
		event.result = result;
		event.hle = hle;
		event.debug = debug;
		event.calls = 1;
		s_events.emplace_back(std::move(event));
	}

	void RecordEeSyscall(const u8 number, const std::string_view name,
		const u32 a0, const u32 a1, const u32 a2, const u32 a3, const s32 result)
	{
		if (!s_enabled.load(std::memory_order_acquire))
			return;
		std::lock_guard lock(s_mutex);
		if (!s_enabled.load(std::memory_order_relaxed))
			return;
		for (Event& event : s_events)
		{
			if (event.kind == "ee_syscall" && event.number == number && event.name == name)
			{
				++event.calls;
				return;
			}
		}
		if (s_events.size() >= MaximumEvents)
		{
			++s_overflow;
			return;
		}
		Event event;
		event.sequence = ++s_next_sequence;
		event.kind = "ee_syscall";
		event.number = number;
		event.name = name;
		event.arguments = {a0, a1, a2, a3};
		event.result = result;
		event.calls = 1;
		s_events.emplace_back(std::move(event));
	}

	void RecordException(const std::string_view domain, const u32 code, const u32 pc,
		const bool branch_delay)
	{
		if (!s_enabled.load(std::memory_order_acquire))
			return;
		std::lock_guard lock(s_mutex);
		if (!s_enabled.load(std::memory_order_relaxed))
			return;
		for (Event& event : s_events)
		{
			if (event.kind == "exception" && event.domain == domain && event.code == code &&
				event.pc == pc && event.branch_delay == branch_delay)
			{
				++event.calls;
				return;
			}
		}
		if (s_events.size() >= MaximumEvents)
		{
			++s_overflow;
			return;
		}
		Event event;
		event.sequence = ++s_next_sequence;
		event.kind = "exception";
		event.domain = domain;
		event.code = code;
		event.pc = pc;
		event.branch_delay = branch_delay;
		event.calls = 1;
		s_events.emplace_back(std::move(event));
	}

	void RecordTimer(const std::string_view domain, const u32 index, const bool overflow,
		const u64 count, const u64 target, const u64 cycle, const bool delivered)
	{
		if (!s_enabled.load(std::memory_order_acquire))
			return;
		std::lock_guard lock(s_mutex);
		if (!s_enabled.load(std::memory_order_relaxed))
			return;
		for (Event& event : s_events)
		{
			if (event.kind == "timer" && event.domain == domain && event.counter == index &&
				event.overflow == overflow && event.delivered == delivered)
			{
				++event.calls;
				return;
			}
		}
		if (s_events.size() >= MaximumEvents)
		{
			++s_overflow;
			return;
		}
		Event event;
		event.sequence = ++s_next_sequence;
		event.kind = "timer";
		event.domain = domain;
		event.counter = index;
		event.overflow = overflow;
		event.count = count;
		event.target = target;
		event.cycle = cycle;
		event.delivered = delivered;
		event.calls = 1;
		s_events.emplace_back(std::move(event));
	}

	void RecordModule(const std::string_view module, const u8 major, const u8 minor,
		const std::string_view operation)
	{
		AddEvent([&](Event& event) {
			event.kind = "module";
			event.operation = operation;
			event.module = module;
			event.major = major;
			event.minor = minor;
		});
	}

	void RecordInterrupt(const u32 number, const std::string_view name, const u32 handler)
	{
		AddEvent([&](Event& event) {
			event.kind = "interrupt";
			event.number = number;
			event.name = name;
			event.handler = handler;
		});
	}

	void RecordRpc(const u32 rpc_id)
	{
		AddEvent([&](Event& event) {
			event.kind = "rpc";
			event.rpc_id = rpc_id;
		});
	}

	std::string SnapshotJson()
	{
		std::lock_guard lock(s_mutex);
		return SnapshotJsonLocked(false);
	}

	std::string SnapshotAndDisableJson()
	{
		std::lock_guard lock(s_mutex);
		return SnapshotJsonLocked(true);
	}

	std::string CaptureAtGuestBoundaryJson(const std::chrono::milliseconds timeout)
	{
		std::unique_lock lock(s_mutex);
		if (!s_enabled.load(std::memory_order_relaxed))
			return {};

		s_capture_requested = true;
		s_capture_result.clear();
		if (!s_capture_condition.wait_for(lock, timeout, []() { return !s_capture_requested; }))
		{
			s_capture_requested = false;
			return {};
		}
		return std::move(s_capture_result);
	}

	void OnGuestFrameBoundary()
	{
		std::lock_guard lock(s_mutex);
		if (!s_capture_requested)
			return;
		s_capture_result = SnapshotJsonLocked(true);
		s_capture_requested = false;
		s_capture_condition.notify_all();
	}

	void StartMissionBoundary()
	{
		std::lock_guard lock(s_mutex);
		ResetObservationsLocked();
		s_enabled.store(true, std::memory_order_release);
		s_mission_armed.store(true, std::memory_order_release);
	}

	bool ShouldInstrumentMissionBoundary(const u32 pc)
	{
		return s_mission_armed.load(std::memory_order_acquire) &&
		       (pc == MissionEntryPc || pc == MissionReturnPc);
	}

	void ObserveMissionBoundary(const u32 pc)
	{
		if (!ShouldInstrumentMissionBoundary(pc))
			return;
		std::lock_guard lock(s_mutex);
		if (!s_mission_armed.load(std::memory_order_relaxed))
			return;
		if (pc == MissionEntryPc)
		{
			if (s_mission_entry || s_mission_return)
				++s_mission_sequence_errors;
			else
				s_mission_entry = LoadTimingPoint::CaptureNext(s_mission_ordinal);
			return;
		}
		if (!s_mission_entry || s_mission_return)
		{
			++s_mission_sequence_errors;
			return;
		}
		s_mission_return = LoadTimingPoint::CaptureNext(s_mission_ordinal);
		s_mission_armed.store(false, std::memory_order_release);
		s_mission_result = BuildMissionResultLocked();
		s_mission_condition.notify_all();
	}

	std::string CaptureMissionBoundaryJson(const std::chrono::milliseconds timeout)
	{
		std::unique_lock lock(s_mutex);
		if (!s_enabled.load(std::memory_order_relaxed) && s_mission_result.empty())
			return {};
		if (s_mission_result.empty() &&
			!s_mission_condition.wait_for(lock, timeout, []() { return !s_mission_result.empty(); }))
		{
			s_mission_armed.store(false, std::memory_order_release);
			s_enabled.store(false, std::memory_order_release);
			return {};
		}
		return std::move(s_mission_result);
	}
} // namespace AVPE::NativeBiosTrace
