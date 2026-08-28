// AVP:E BIOS/IOP observation trace. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeBiosTrace.h"

#include <array>
#include <atomic>
#include <mutex>
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
			u8 major = 0;
			u8 minor = 0;
			u32 number = 0;
			u32 handler = 0;
			u32 rpc_id = 0;
			bool hle = false;
			bool debug = false;
			bool branch_delay = false;
		};

		std::mutex s_mutex;
		std::vector<Event> s_events;
		u64 s_next_sequence = 0;
		u64 s_overflow = 0;

		std::atomic_bool s_enabled{false};

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
			}
			else if (event.kind == "ee_syscall")
			{
				json += ",\"number\":" + std::to_string(event.number);
				AppendString(json, "name", event.name);
				AppendArguments(json, event.arguments);
				json += ",\"result\":" + std::to_string(event.result);
			}
			else if (event.kind == "exception")
			{
				AppendString(json, "domain", event.domain);
				json += ",\"code\":" + std::to_string(event.code);
				json += ",\"pc\":" + std::to_string(event.pc);
				json += ",\"branch_delay\":" + std::string(event.branch_delay ? "true" : "false");
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

	void Reset()
	{
		std::lock_guard lock(s_mutex);
		s_events.clear();
		s_next_sequence = 0;
		s_overflow = 0;
	}

	void SetEnabled(const bool enabled)
	{
		std::lock_guard lock(s_mutex);
		s_enabled.store(enabled, std::memory_order_release);
		s_events.clear();
		s_next_sequence = 0;
		s_overflow = 0;
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
		AddEvent([&](Event& event) {
			event.kind = "import";
			event.library = library;
			event.ordinal = ordinal;
			event.function = function;
			event.arguments = {a0, a1, a2, a3};
			event.result = result;
			event.hle = hle;
			event.debug = debug;
		});
	}

	void RecordEeSyscall(const u8 number, const std::string_view name,
		const u32 a0, const u32 a1, const u32 a2, const u32 a3, const s32 result)
	{
		AddEvent([&](Event& event) {
			event.kind = "ee_syscall";
			event.number = number;
			event.name = name;
			event.arguments = {a0, a1, a2, a3};
			event.result = result;
		});
	}

	void RecordException(const std::string_view domain, const u32 code, const u32 pc,
		const bool branch_delay)
	{
		AddEvent([&](Event& event) {
			event.kind = "exception";
			event.domain = domain;
			event.code = code;
			event.pc = pc;
			event.branch_delay = branch_delay;
		});
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
		std::string json = "{\"schema\":\"avpe-bios-trace-v1\",\"enabled\":";
		json += s_enabled.load(std::memory_order_relaxed) ? "true" : "false";
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
} // namespace AVPE::NativeBiosTrace
