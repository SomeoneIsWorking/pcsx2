// AVP:E BIOS/IOP observation trace. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeBiosTrace.h"

#include "AVPE/LoadTimingPoint.h"

#include <algorithm>
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
			std::string outcome;
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
			bool result_valid = false;
		};

		struct TimingTotals
		{
			u64 samples = 0;
			u64 ee_cycles = 0;
			u64 iop_cycles = 0;
			u64 frames = 0;
			u64 host_time_ns = 0;
		};

		struct TypeInitializerFrame
		{
			LoadTimingPoint::Point call;
			u32 target = 0;
			u32 object = 0;
			u32 descriptor = 0;
			u32 remaining = 0;
			u32 stack_pointer = 0;
			u32 symbol = 0;
			u32 metadata = 0;
			bool descriptor_valid = false;
		};

		struct ObjectFactoryFrame
		{
			LoadTimingPoint::Point call;
			u32 target = 0;
			u32 class_entry = 0;
			u32 handle = 0;
			u32 fill_data = 0;
			u32 stack_pointer = 0;
		};

		enum class PostReadStage : u8
		{
			EofDetected,
			WatchReleased,
			FixupOffsetsComplete,
			FixupExternsComplete,
			SetupPublicsComplete,
			FixupHandlesComplete,
			InitTypesComplete,
			LoadCoreReturn,
			Count,
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
		std::optional<LoadTimingPoint::Point> s_mission_load_error;
		u32 s_mission_load_error_argument = 0;
		u32 s_mission_load_error_return_pc = 0;
		u64 s_mission_chunks_started = 0;
		u64 s_mission_chunks_completed = 0;
		u64 s_mission_load_callbacks = 0;
		u32 s_mission_active_chunk_size = 0;
		u32 s_mission_last_remaining = 0;
		u32 s_mission_load_callback_pc = 0;
		u32 s_mission_invalid_remaining_reads = 0;
		u64 s_mission_payload_bytes = 0;
		u64 s_mission_payload_chunks = 0;
		u64 s_mission_multi_slice_chunks = 0;
		u32 s_mission_last_payload_size = 0;
		u32 s_mission_max_payload_size = 0;
		u32 s_mission_active_callbacks = 0;
		bool s_mission_active_payload_accounted = false;
		std::optional<LoadTimingPoint::Point> s_mission_chunk_timing_start;
		std::optional<LoadTimingPoint::Point> s_mission_callback_timing_start;
		std::optional<LoadTimingPoint::Point> s_mission_payload_timing_start;
		std::optional<LoadTimingPoint::Point> s_mission_first_chunk_entry;
		std::optional<LoadTimingPoint::Point> s_mission_previous_chunk_return;
		TimingTotals s_mission_chunk_timing;
		TimingTotals s_mission_callback_timing;
		TimingTotals s_mission_payload_timing;
		TimingTotals s_mission_inter_chunk_timing;
		std::array<std::optional<LoadTimingPoint::Point>, static_cast<size_t>(PostReadStage::Count)>
			s_mission_post_read_points;
		std::array<u64, static_cast<size_t>(PostReadStage::Count)> s_mission_post_read_counts{};
		std::array<u32, 8> s_mission_post_read_parent_stages{};
		u32 s_mission_post_read_depth = 0;
		u32 s_mission_post_read_next_stage = 0;
		u32 s_mission_post_read_sequence_errors = 0;
		std::array<TypeInitializerFrame, 8> s_mission_type_initializer_stack{};
		u32 s_mission_type_initializer_depth = 0;
		u32 s_mission_type_initializer_max_depth = 0;
		u64 s_mission_type_initializer_calls = 0;
		u64 s_mission_type_initializer_returns = 0;
		u32 s_mission_type_initializer_sequence_errors = 0;
		u32 s_mission_type_initializer_invalid_descriptors = 0;
		std::optional<TypeInitializerFrame> s_mission_last_type_initializer;
		std::optional<LoadTimingPoint::Point> s_mission_last_type_initializer_return;
		std::array<ObjectFactoryFrame, 8> s_mission_object_factory_stack{};
		u32 s_mission_object_factory_depth = 0;
		u32 s_mission_object_factory_max_depth = 0;
		u64 s_mission_object_factory_calls = 0;
		u64 s_mission_object_factory_returns = 0;
		u32 s_mission_object_factory_sequence_errors = 0;
		std::optional<ObjectFactoryFrame> s_mission_last_object_factory;
		std::optional<LoadTimingPoint::Point> s_mission_last_object_factory_return;
		u64 s_mission_progress_ordinal = 0;
		u32 s_mission_timing_sequence_errors = 0;
		u64 s_mission_ordinal = 0;
		u32 s_mission_sequence_errors = 0;

		std::atomic_bool s_enabled{false};
		std::atomic_bool s_mission_armed{false};

		constexpr u32 MissionEntryPc = 0x0016F910;
		constexpr u32 MissionReturnPc = 0x0016FA4C;
		constexpr u32 MissionLoadErrorPc = 0x00173770;
		constexpr u32 MissionReadChunkEntryPc = 0x00173CB0;
		constexpr u32 MissionReadChunkCallbackPc = 0x00173D90;
		constexpr u32 MissionReadChunkCallbackReturnPc = 0x00173D98;
		constexpr u32 MissionReadChunkReturnPc = 0x00173E34;
		constexpr u32 MissionTypeInitializerCallPc = 0x0017467C;
		constexpr u32 MissionTypeInitializerReturnPc = 0x00174684;
		constexpr u32 MissionObjectFactoryCallPc = 0x00127EC0;
		constexpr u32 MissionObjectFactoryReturnPc = 0x00127EC8;
		constexpr std::array<u32, static_cast<size_t>(PostReadStage::Count)> MissionPostReadPcs = {
			0x00173FFC,
			0x00174164,
			0x00174178,
			0x00174180,
			0x00174188,
			0x00174190,
			0x00174198,
			0x001741E0,
		};
		constexpr std::array<std::string_view, static_cast<size_t>(PostReadStage::Count)> MissionPostReadNames = {
			"eof_detected",
			"watch_released",
			"fixup_offsets_complete",
			"fixup_externs_complete",
			"setup_publics_complete",
			"fixup_handles_complete",
			"init_types_complete",
			"load_core_return",
		};

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

		void AppendTimingTotals(std::string& json, const TimingTotals& totals)
		{
			json += "{\"samples\":" + std::to_string(totals.samples);
			json += ",\"ee_cycles\":" + std::to_string(totals.ee_cycles);
			json += ",\"iop_cycles\":" + std::to_string(totals.iop_cycles);
			json += ",\"frames\":" + std::to_string(totals.frames);
			json += ",\"host_time_ns\":" + std::to_string(totals.host_time_ns) + '}';
		}

		void AccumulateTiming(const LoadTimingPoint::Point& start, const LoadTimingPoint::Point& end,
			TimingTotals& totals)
		{
			if (end.ee_cycle < start.ee_cycle || end.iop_cycle < start.iop_cycle || end.frame < start.frame ||
				end.host_time_ns < start.host_time_ns)
			{
				++s_mission_timing_sequence_errors;
				return;
			}
			++totals.samples;
			totals.ee_cycles += end.ee_cycle - start.ee_cycle;
			totals.iop_cycles += end.iop_cycle - start.iop_cycle;
			totals.frames += end.frame - start.frame;
			totals.host_time_ns += end.host_time_ns - start.host_time_ns;
		}

		std::optional<size_t> PostReadStageIndex(const u32 pc)
		{
			const auto it = std::find(MissionPostReadPcs.begin(), MissionPostReadPcs.end(), pc);
			if (it == MissionPostReadPcs.end())
				return std::nullopt;
			return static_cast<size_t>(std::distance(MissionPostReadPcs.begin(), it));
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
					json += ",\"result\":" + std::to_string(event.result);
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
			std::string json = "{\"schema\":\"avpe-bios-trace-v2\",\"enabled\":";
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
			s_mission_load_error.reset();
			s_mission_load_error_argument = 0;
			s_mission_load_error_return_pc = 0;
			s_mission_chunks_started = 0;
			s_mission_chunks_completed = 0;
			s_mission_load_callbacks = 0;
			s_mission_active_chunk_size = 0;
			s_mission_last_remaining = 0;
			s_mission_load_callback_pc = 0;
			s_mission_invalid_remaining_reads = 0;
			s_mission_payload_bytes = 0;
			s_mission_payload_chunks = 0;
			s_mission_multi_slice_chunks = 0;
			s_mission_last_payload_size = 0;
			s_mission_max_payload_size = 0;
			s_mission_active_callbacks = 0;
			s_mission_active_payload_accounted = false;
			s_mission_chunk_timing_start.reset();
			s_mission_callback_timing_start.reset();
			s_mission_payload_timing_start.reset();
			s_mission_first_chunk_entry.reset();
			s_mission_previous_chunk_return.reset();
			s_mission_chunk_timing = {};
			s_mission_callback_timing = {};
			s_mission_payload_timing = {};
			s_mission_inter_chunk_timing = {};
			s_mission_post_read_points = {};
			s_mission_post_read_counts = {};
			s_mission_post_read_parent_stages = {};
			s_mission_post_read_depth = 0;
			s_mission_post_read_next_stage = 0;
			s_mission_post_read_sequence_errors = 0;
			s_mission_type_initializer_stack = {};
			s_mission_type_initializer_depth = 0;
			s_mission_type_initializer_max_depth = 0;
			s_mission_type_initializer_calls = 0;
			s_mission_type_initializer_returns = 0;
			s_mission_type_initializer_sequence_errors = 0;
			s_mission_type_initializer_invalid_descriptors = 0;
			s_mission_last_type_initializer.reset();
			s_mission_last_type_initializer_return.reset();
			s_mission_object_factory_stack = {};
			s_mission_object_factory_depth = 0;
			s_mission_object_factory_max_depth = 0;
			s_mission_object_factory_calls = 0;
			s_mission_object_factory_returns = 0;
			s_mission_object_factory_sequence_errors = 0;
			s_mission_last_object_factory.reset();
			s_mission_last_object_factory_return.reset();
			s_mission_progress_ordinal = 0;
			s_mission_timing_sequence_errors = 0;
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

		void AppendTypeInitializer(std::string& json, const TypeInitializerFrame& frame)
		{
			json += "{\"target\":" + std::to_string(frame.target);
			json += ",\"object\":" + std::to_string(frame.object);
			json += ",\"descriptor\":" + std::to_string(frame.descriptor);
			json += ",\"remaining\":" + std::to_string(frame.remaining);
			json += ",\"stack_pointer\":" + std::to_string(frame.stack_pointer);
			json += ",\"symbol\":" + std::to_string(frame.symbol);
			json += ",\"metadata\":" + std::to_string(frame.metadata);
			json += ",\"descriptor_valid\":";
			json += frame.descriptor_valid ? "true" : "false";
			json += ",\"call\":";
			AppendMissionPoint(json, frame.call, MissionTypeInitializerCallPc);
			json += '}';
		}

		void AppendObjectFactory(std::string& json, const ObjectFactoryFrame& frame)
		{
			json += "{\"target\":" + std::to_string(frame.target);
			json += ",\"class_entry\":" + std::to_string(frame.class_entry);
			json += ",\"handle\":" + std::to_string(frame.handle);
			json += ",\"fill_data\":" + std::to_string(frame.fill_data);
			json += ",\"stack_pointer\":" + std::to_string(frame.stack_pointer);
			json += ",\"call\":";
			AppendMissionPoint(json, frame.call, MissionObjectFactoryCallPc);
			json += '}';
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
			json += ",\"load_error\":";
			if (s_mission_load_error)
			{
				json += "{\"argument\":" + std::to_string(s_mission_load_error_argument);
				json += ",\"return_pc\":" + std::to_string(s_mission_load_error_return_pc);
				json += ",\"point\":";
				AppendMissionPoint(json, *s_mission_load_error, MissionLoadErrorPc);
				json += '}';
			}
			else
				json += "null";
			json += ",\"load_progress\":{\"chunks_started\":" + std::to_string(s_mission_chunks_started);
			json += ",\"chunks_completed\":" + std::to_string(s_mission_chunks_completed);
			json += ",\"callbacks\":" + std::to_string(s_mission_load_callbacks);
			json += ",\"active_chunk_size\":" + std::to_string(s_mission_active_chunk_size);
			json += ",\"last_remaining\":" + std::to_string(s_mission_last_remaining);
			json += ",\"callback_pc\":" + std::to_string(s_mission_load_callback_pc);
			json += ",\"invalid_remaining_reads\":" + std::to_string(s_mission_invalid_remaining_reads);
			json += ",\"payload_bytes\":" + std::to_string(s_mission_payload_bytes);
			json += ",\"payload_chunks\":" + std::to_string(s_mission_payload_chunks);
			json += ",\"multi_slice_chunks\":" + std::to_string(s_mission_multi_slice_chunks);
			json += ",\"last_payload_size\":" + std::to_string(s_mission_last_payload_size);
			json += ",\"max_payload_size\":" + std::to_string(s_mission_max_payload_size);
			json += ",\"timing_sequence_errors\":" + std::to_string(s_mission_timing_sequence_errors);
			json += ",\"first_chunk_entry\":";
			if (s_mission_first_chunk_entry)
				AppendMissionPoint(json, *s_mission_first_chunk_entry, MissionReadChunkEntryPc);
			else
				json += "null";
			json += ",\"last_chunk_return\":";
			if (s_mission_previous_chunk_return)
				AppendMissionPoint(json, *s_mission_previous_chunk_return, MissionReadChunkReturnPc);
			else
				json += "null";
			json += ",\"chunk_timing\":";
			AppendTimingTotals(json, s_mission_chunk_timing);
			json += ",\"callback_timing\":";
			AppendTimingTotals(json, s_mission_callback_timing);
			json += ",\"payload_timing\":";
			AppendTimingTotals(json, s_mission_payload_timing);
			json += ",\"inter_chunk_timing\":";
			AppendTimingTotals(json, s_mission_inter_chunk_timing);
			json += '}';
			json += ",\"post_read_progress\":{\"sequence_errors\":" +
			        std::to_string(s_mission_post_read_sequence_errors);
			json += ",\"next_expected\":\"" +
			        std::string(MissionPostReadNames[s_mission_post_read_next_stage]) + '"';
			json += ",\"active_depth\":" + std::to_string(s_mission_post_read_depth);
			for (size_t i = 0; i < MissionPostReadNames.size(); ++i)
			{
				json += ",\"" + std::string(MissionPostReadNames[i]) + "\":{\"count\":" +
				        std::to_string(s_mission_post_read_counts[i]) + ",\"last\":";
				if (s_mission_post_read_points[i])
					AppendMissionPoint(json, *s_mission_post_read_points[i], MissionPostReadPcs[i]);
				else
					json += "null";
				json += '}';
			}
			json += '}';
			json += ",\"type_initializer_progress\":{\"calls\":" +
			        std::to_string(s_mission_type_initializer_calls);
			json += ",\"returns\":" + std::to_string(s_mission_type_initializer_returns);
			json += ",\"active_depth\":" + std::to_string(s_mission_type_initializer_depth);
			json += ",\"max_depth\":" + std::to_string(s_mission_type_initializer_max_depth);
			json += ",\"sequence_errors\":" + std::to_string(s_mission_type_initializer_sequence_errors);
			json += ",\"invalid_descriptors\":" +
			        std::to_string(s_mission_type_initializer_invalid_descriptors);
			json += ",\"active\":[";
			for (u32 i = 0; i < s_mission_type_initializer_depth; ++i)
			{
				if (i != 0)
					json += ',';
				AppendTypeInitializer(json, s_mission_type_initializer_stack[i]);
			}
			json += "],\"last_completed\":";
			if (s_mission_last_type_initializer)
				AppendTypeInitializer(json, *s_mission_last_type_initializer);
			else
				json += "null";
			json += ",\"last_return\":";
			if (s_mission_last_type_initializer_return)
				AppendMissionPoint(json, *s_mission_last_type_initializer_return, MissionTypeInitializerReturnPc);
			else
				json += "null";
			json += '}';
			json += ",\"object_factory_progress\":{\"calls\":" +
			        std::to_string(s_mission_object_factory_calls);
			json += ",\"returns\":" + std::to_string(s_mission_object_factory_returns);
			json += ",\"active_depth\":" + std::to_string(s_mission_object_factory_depth);
			json += ",\"max_depth\":" + std::to_string(s_mission_object_factory_max_depth);
			json += ",\"sequence_errors\":" + std::to_string(s_mission_object_factory_sequence_errors);
			json += ",\"active\":[";
			for (u32 i = 0; i < s_mission_object_factory_depth; ++i)
			{
				if (i != 0)
					json += ',';
				AppendObjectFactory(json, s_mission_object_factory_stack[i]);
			}
			json += "],\"last_completed\":";
			if (s_mission_last_object_factory)
				AppendObjectFactory(json, *s_mission_last_object_factory);
			else
				json += "null";
			json += ",\"last_return\":";
			if (s_mission_last_object_factory_return)
				AppendMissionPoint(json, *s_mission_last_object_factory_return, MissionObjectFactoryReturnPc);
			else
				json += "null";
			json += '}';
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
		return s_enabled.load(std::memory_order_acquire);
	}

	void RecordImport(const std::string_view library, const u16 ordinal, const std::string_view function,
		const u32 a0, const u32 a1, const u32 a2, const u32 a3, const s32 result,
		const bool hle, const bool debug, const bool handled)
	{
		if (!s_enabled.load(std::memory_order_acquire))
			return;
		std::lock_guard lock(s_mutex);
		if (!s_enabled.load(std::memory_order_relaxed))
			return;
		for (Event& event : s_events)
		{
			if (event.kind == "import" && event.library == library && event.ordinal == ordinal &&
				event.function == function && event.hle == hle && event.debug == debug &&
				event.result_valid == handled && (!handled || event.result == result))
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
		event.result_valid = handled;
		event.outcome = handled ? "hle" : "oracle";
		event.hle = hle;
		event.debug = debug;
		event.calls = 1;
		s_events.emplace_back(std::move(event));
	}

	void RecordEeSyscall(const u8 number, const std::string_view name,
		const u32 a0, const u32 a1, const u32 a2, const u32 a3, const s32 result,
		const bool result_valid)
	{
		if (!s_enabled.load(std::memory_order_acquire))
			return;
		std::lock_guard lock(s_mutex);
		if (!s_enabled.load(std::memory_order_relaxed))
			return;
		for (Event& event : s_events)
		{
			if (event.kind == "ee_syscall" && event.number == number && event.name == name &&
				event.result_valid == result_valid && (!result_valid || event.result == result))
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
		event.result_valid = result_valid;
		event.outcome = result_valid ? "direct" : "bios";
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
		// This predicate runs while the EE recompiler translates blocks, before
		// the control request can arm a phase. Keep the grounded mission PCs
		// instrumented permanently; the observe functions apply the runtime arm
		// gate so pre-phase execution cannot become evidence.
		return pc == MissionEntryPc || pc == MissionReturnPc || pc == MissionLoadErrorPc ||
		       pc == MissionTypeInitializerCallPc || pc == MissionTypeInitializerReturnPc ||
		       pc == MissionObjectFactoryCallPc || pc == MissionObjectFactoryReturnPc || PostReadStageIndex(pc) ||
		       pc == MissionReadChunkEntryPc ||
		       pc == MissionReadChunkCallbackPc || pc == MissionReadChunkCallbackReturnPc ||
		       pc == MissionReadChunkReturnPc;
	}

	void ObserveMissionTypeInitializer(const u32 pc, const u32 target, const u32 object, const u32 descriptor,
		const u32 remaining, const u32 stack_pointer, const u32 symbol, const u32 metadata,
		const bool descriptor_valid)
	{
		if (pc != MissionTypeInitializerCallPc && pc != MissionTypeInitializerReturnPc)
			return;
		std::lock_guard lock(s_mutex);
		if (!s_mission_armed.load(std::memory_order_relaxed) || !s_mission_entry)
			return;
		if (pc == MissionTypeInitializerCallPc)
		{
			++s_mission_type_initializer_calls;
			if (!descriptor_valid)
				++s_mission_type_initializer_invalid_descriptors;
			if (s_mission_type_initializer_depth == s_mission_type_initializer_stack.size())
			{
				++s_mission_type_initializer_sequence_errors;
				return;
			}
			s_mission_type_initializer_stack[s_mission_type_initializer_depth++] = {
				.call = LoadTimingPoint::CaptureNext(s_mission_progress_ordinal),
				.target = target,
				.object = object,
				.descriptor = descriptor,
				.remaining = remaining,
				.stack_pointer = stack_pointer,
				.symbol = symbol,
				.metadata = metadata,
				.descriptor_valid = descriptor_valid,
			};
			s_mission_type_initializer_max_depth =
				std::max(s_mission_type_initializer_max_depth, s_mission_type_initializer_depth);
			return;
		}
		if (s_mission_type_initializer_depth == 0 ||
			s_mission_type_initializer_stack[s_mission_type_initializer_depth - 1].stack_pointer != stack_pointer)
		{
			return;
		}
		++s_mission_type_initializer_returns;
		s_mission_last_type_initializer = s_mission_type_initializer_stack[--s_mission_type_initializer_depth];
		s_mission_last_type_initializer_return = LoadTimingPoint::CaptureNext(s_mission_progress_ordinal);
	}

	void ObserveMissionObjectFactory(const u32 pc, const u32 target, const u32 class_entry, const u32 handle,
		const u32 fill_data, const u32 stack_pointer)
	{
		if (pc != MissionObjectFactoryCallPc && pc != MissionObjectFactoryReturnPc)
			return;
		std::lock_guard lock(s_mutex);
		if (!s_mission_armed.load(std::memory_order_relaxed) || !s_mission_entry)
			return;
		if (pc == MissionObjectFactoryCallPc)
		{
			++s_mission_object_factory_calls;
			if (s_mission_object_factory_depth == s_mission_object_factory_stack.size())
			{
				++s_mission_object_factory_sequence_errors;
				return;
			}
			s_mission_object_factory_stack[s_mission_object_factory_depth++] = {
				.call = LoadTimingPoint::CaptureNext(s_mission_progress_ordinal),
				.target = target,
				.class_entry = class_entry,
				.handle = handle,
				.fill_data = fill_data,
				.stack_pointer = stack_pointer,
			};
			s_mission_object_factory_max_depth =
				std::max(s_mission_object_factory_max_depth, s_mission_object_factory_depth);
			return;
		}
		if (s_mission_object_factory_depth == 0 ||
			s_mission_object_factory_stack[s_mission_object_factory_depth - 1].stack_pointer != stack_pointer)
		{
			return;
		}
		++s_mission_object_factory_returns;
		s_mission_last_object_factory = s_mission_object_factory_stack[--s_mission_object_factory_depth];
		s_mission_last_object_factory_return = LoadTimingPoint::CaptureNext(s_mission_progress_ordinal);
	}

	void ObserveMissionPostReadProgress(const u32 pc, const u32 chunk_descriptor)
	{
		const std::optional<size_t> stage = PostReadStageIndex(pc);
		if (!stage || (*stage == static_cast<size_t>(PostReadStage::EofDetected) && chunk_descriptor != 0))
			return;
		std::lock_guard lock(s_mutex);
		if (!s_mission_armed.load(std::memory_order_relaxed) || !s_mission_entry)
			return;
		if (*stage == static_cast<size_t>(PostReadStage::EofDetected) && s_mission_post_read_next_stage != 0)
		{
			if (s_mission_post_read_depth == s_mission_post_read_parent_stages.size())
				++s_mission_post_read_sequence_errors;
			else
				s_mission_post_read_parent_stages[s_mission_post_read_depth++] = s_mission_post_read_next_stage;
			s_mission_post_read_next_stage = 0;
		}
		if (*stage != s_mission_post_read_next_stage)
			++s_mission_post_read_sequence_errors;
		++s_mission_post_read_counts[*stage];
		s_mission_post_read_points[*stage] = LoadTimingPoint::CaptureNext(s_mission_progress_ordinal);
		if (*stage == static_cast<size_t>(PostReadStage::LoadCoreReturn) && s_mission_post_read_depth != 0)
			s_mission_post_read_next_stage = s_mission_post_read_parent_stages[--s_mission_post_read_depth];
		else
			s_mission_post_read_next_stage =
				static_cast<u32>((*stage + 1) % static_cast<size_t>(PostReadStage::Count));
	}

	void ObserveMissionBoundary(const u32 pc)
	{
		if (pc != MissionEntryPc && pc != MissionReturnPc)
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

	void ObserveMissionLoadError(const u32 pc, const u32 argument, const u32 return_pc)
	{
		if (pc != MissionLoadErrorPc)
			return;
		std::lock_guard lock(s_mutex);
		if (!s_mission_armed.load(std::memory_order_relaxed) || !s_mission_entry || s_mission_load_error)
			return;
		s_mission_load_error = LoadTimingPoint::CaptureNext(s_mission_ordinal);
		s_mission_load_error_argument = argument;
		s_mission_load_error_return_pc = return_pc;
		s_mission_armed.store(false, std::memory_order_release);
		s_mission_result = BuildMissionResultLocked();
		s_mission_condition.notify_all();
	}

	void ObserveMissionLoadProgress(const u32 pc, const u32 chunk_size, const u32 callback_pc,
		const u32 stack_remaining, const bool stack_remaining_valid)
	{
		if (pc != MissionReadChunkEntryPc && pc != MissionReadChunkCallbackPc &&
			pc != MissionReadChunkCallbackReturnPc && pc != MissionReadChunkReturnPc)
			return;
		std::lock_guard lock(s_mutex);
		if (!s_mission_armed.load(std::memory_order_relaxed) || !s_mission_entry)
			return;
		if (pc == MissionReadChunkEntryPc)
		{
			if (s_mission_chunk_timing_start || s_mission_callback_timing_start || s_mission_payload_timing_start)
				++s_mission_timing_sequence_errors;
			const LoadTimingPoint::Point start = LoadTimingPoint::CaptureNext(s_mission_progress_ordinal);
			if (!s_mission_first_chunk_entry)
				s_mission_first_chunk_entry = start;
			if (s_mission_previous_chunk_return)
				AccumulateTiming(*s_mission_previous_chunk_return, start, s_mission_inter_chunk_timing);
			s_mission_chunk_timing_start = start;
			s_mission_callback_timing_start.reset();
			s_mission_payload_timing_start.reset();
			++s_mission_chunks_started;
			s_mission_active_chunk_size = chunk_size;
			s_mission_last_remaining = chunk_size;
			s_mission_active_callbacks = 0;
			s_mission_active_payload_accounted = false;
			return;
		}
		if (pc == MissionReadChunkReturnPc)
		{
			const LoadTimingPoint::Point end = LoadTimingPoint::CaptureNext(s_mission_progress_ordinal);
			if (s_mission_callback_timing_start)
			{
				++s_mission_timing_sequence_errors;
				s_mission_callback_timing_start.reset();
			}
			if (s_mission_payload_timing_start)
			{
				AccumulateTiming(*s_mission_payload_timing_start, end, s_mission_payload_timing);
				s_mission_payload_timing_start.reset();
			}
			if (s_mission_chunk_timing_start)
			{
				AccumulateTiming(*s_mission_chunk_timing_start, end, s_mission_chunk_timing);
				s_mission_chunk_timing_start.reset();
			}
			else
				++s_mission_timing_sequence_errors;
			s_mission_previous_chunk_return = end;
			++s_mission_chunks_completed;
			s_mission_active_chunk_size = 0;
			s_mission_last_remaining = 0;
			s_mission_active_callbacks = 0;
			s_mission_active_payload_accounted = false;
			return;
		}
		const LoadTimingPoint::Point now = LoadTimingPoint::CaptureNext(s_mission_progress_ordinal);
		if (pc == MissionReadChunkCallbackReturnPc)
		{
			if (s_mission_callback_timing_start)
			{
				AccumulateTiming(*s_mission_callback_timing_start, now, s_mission_callback_timing);
				s_mission_callback_timing_start.reset();
			}
			else
				++s_mission_timing_sequence_errors;
			s_mission_payload_timing_start = now;
			return;
		}
		if (s_mission_payload_timing_start)
		{
			AccumulateTiming(*s_mission_payload_timing_start, now, s_mission_payload_timing);
			s_mission_payload_timing_start.reset();
		}
		if (s_mission_callback_timing_start)
			++s_mission_timing_sequence_errors;
		s_mission_callback_timing_start = now;
		++s_mission_load_callbacks;
		s_mission_load_callback_pc = callback_pc;
		if (stack_remaining_valid)
		{
			s_mission_last_remaining = stack_remaining;
			if (!s_mission_active_payload_accounted)
			{
				s_mission_payload_bytes += stack_remaining;
				++s_mission_payload_chunks;
				s_mission_last_payload_size = stack_remaining;
				s_mission_max_payload_size = std::max(s_mission_max_payload_size, stack_remaining);
				s_mission_active_payload_accounted = true;
			}
		}
		else
			++s_mission_invalid_remaining_reads;
		if (s_mission_active_callbacks == 1)
			++s_mission_multi_slice_chunks;
		++s_mission_active_callbacks;
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
			s_mission_result = BuildMissionResultLocked();
		}
		return std::move(s_mission_result);
	}
} // namespace AVPE::NativeBiosTrace
