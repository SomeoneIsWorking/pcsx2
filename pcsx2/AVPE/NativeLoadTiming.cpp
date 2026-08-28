// AVP:E native/optical loading-boundary evidence. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeLoadTiming.h"

#include "AVPE/AVPE.h"
#include "AVPE/LoadTimingPoint.h"
#include "VMManager.h"

#include <cstdlib>
#include <mutex>
#include <optional>
#include <string_view>

namespace AVPE::NativeLoadTiming
{
	namespace
	{
		constexpr std::string_view kSchema = "avpe-load-timing-v1";
		constexpr std::string_view kTargetSerial = "SLUS-20147";
		constexpr std::string_view kModeEnvironment = "AVPE_LOAD_TIMING";

		std::mutex s_mutex;
		std::optional<LoadTimingPoint::Point> s_start;
		std::optional<LoadTimingPoint::Point> s_end;
		u64 s_ordinal = 0;
		u64 s_sequence_errors = 0;
		bool s_menu_search_pending = false;
		std::optional<Backend> s_tbf_backend;
		std::optional<Backend> s_menu_seek_backend;

		std::optional<std::string_view> Mode()
		{
			const char* const configured = std::getenv(kModeEnvironment.data());
			if (!configured)
				return std::nullopt;
			const std::string_view mode(configured);
			return mode == "oracle" || mode == "native" ? std::optional<std::string_view>(mode) : std::nullopt;
		}

		bool IsEnabled()
		{
			return IsSurfacelessControlTest() && VMManager::GetDiscSerial() == kTargetSerial && Mode().has_value();
		}

		std::string_view BackendName(const Backend backend)
		{
			switch (backend)
			{
				case Backend::Optical:
					return "optical";
				case Backend::Native:
					return "native";
				case Backend::Refused:
					return "refused";
			}
			return "refused";
		}

		void AppendPoint(std::string& body, const std::string_view kind, const std::string_view path,
			const LoadTimingPoint::Point& point)
		{
			body += "{\"kind\":\"" + std::string(kind) + "\",\"path\":\"" + std::string(path) + "\"";
			body += ",\"ordinal\":" + std::to_string(point.ordinal);
			body += ",\"ee_cycle\":" + std::to_string(point.ee_cycle);
			body += ",\"iop_cycle\":" + std::to_string(point.iop_cycle);
			body += ",\"frame\":" + std::to_string(point.frame);
			body += ",\"host_time_ns\":" + std::to_string(point.host_time_ns) + '}';
		}

		bool BackendsMatchMode(const std::string_view mode)
		{
			if (!s_tbf_backend || !s_menu_seek_backend)
				return false;
			const Backend expected = mode == "native" ? Backend::Native : Backend::Optical;
			return *s_tbf_backend == expected && *s_menu_seek_backend == expected;
		}
	} // namespace

	void NoteTbfOpen()
	{
		if (!IsEnabled())
			return;
		std::lock_guard lock(s_mutex);
		if (!s_start && !s_end)
			s_start = LoadTimingPoint::CaptureNext(s_ordinal);
	}

	void NoteTbfBackend(const Backend backend)
	{
		if (!IsEnabled())
			return;
		std::lock_guard lock(s_mutex);
		if (s_start && !s_end && !s_tbf_backend)
			s_tbf_backend = backend;
	}

	void NoteCdvdSearch(const bool menu01)
	{
		if (!IsEnabled())
			return;
		std::lock_guard lock(s_mutex);
		if (s_end && !s_menu_seek_backend)
			return;
		if (s_menu_search_pending)
		{
			++s_sequence_errors;
			s_menu_search_pending = false;
		}
		if (!menu01)
			return;
		if (!s_start)
		{
			++s_sequence_errors;
			return;
		}
		s_menu_search_pending = true;
		++s_ordinal;
	}

	void NoteCdvdSeekBackend(const Backend backend)
	{
		if (!IsEnabled())
			return;
		std::lock_guard lock(s_mutex);
		if (s_end)
			s_menu_seek_backend = backend;
	}

	void NoteCdvdSeek()
	{
		if (!IsEnabled())
			return;
		std::lock_guard lock(s_mutex);
		if (s_end || !s_menu_search_pending)
			return;
		s_menu_search_pending = false;
		s_end = LoadTimingPoint::CaptureNext(s_ordinal);
	}

	std::string SnapshotJson()
	{
		const std::optional<std::string_view> mode = Mode();
		const bool byte_trace_disabled = std::getenv("AVPE_ASSET_BYTE_TRACE") == nullptr;
		std::lock_guard lock(s_mutex);
		const bool complete = mode && byte_trace_disabled && s_start && s_end && BackendsMatchMode(*mode) &&
		                      s_end->ee_cycle > s_start->ee_cycle && s_end->iop_cycle > s_start->iop_cycle &&
		                      s_end->frame > s_start->frame && s_end->host_time_ns > s_start->host_time_ns;
		std::string body = "{\"schema\":\"" + std::string(kSchema) + "\",\"enabled\":";
		body += IsEnabled() ? "true" : "false";
		body += ",\"target_recognized\":";
		body += VMManager::GetDiscSerial() == kTargetSerial ? "true" : "false";
		body += ",\"byte_trace_disabled\":";
		body += byte_trace_disabled ? "true" : "false";
		body += ",\"mode\":\"" + std::string(mode.value_or("disabled")) + "\"";
		body += ",\"complete\":";
		body += complete ? "true" : "false";
		body += ",\"sequence_errors\":" + std::to_string(s_sequence_errors);
		body += ",\"backends\":{\"tbf\":\"";
		body += s_tbf_backend ? BackendName(*s_tbf_backend) : "unresolved";
		body += "\",\"menu01_seek\":\"";
		body += s_menu_seek_backend ? BackendName(*s_menu_seek_backend) : "unresolved";
		body += "\"}";
		body += ",\"start\":";
		if (s_start)
			AppendPoint(body, "tbf-open", "TBD/TBF.TBF", *s_start);
		else
			body += "null";
		body += ",\"end\":";
		if (s_end)
			AppendPoint(body, "menu01-post-search-seek", "STREAMS/MENU01.ZIV", *s_end);
		else
			body += "null";
		body += ",\"deltas\":";
		if (complete)
		{
			body += "{\"ee_cycles\":" + std::to_string(s_end->ee_cycle - s_start->ee_cycle);
			body += ",\"iop_cycles\":" + std::to_string(s_end->iop_cycle - s_start->iop_cycle);
			body += ",\"frames\":" + std::to_string(s_end->frame - s_start->frame);
			body += ",\"host_elapsed_ns\":" +
			        std::to_string(s_end->host_time_ns - s_start->host_time_ns) + '}';
		}
		else
		{
			body += "null";
		}
		body += '}';
		return body;
	}

	void Reset()
	{
		std::lock_guard lock(s_mutex);
		s_start.reset();
		s_end.reset();
		s_ordinal = 0;
		s_sequence_errors = 0;
		s_menu_search_pending = false;
		s_tbf_backend.reset();
		s_menu_seek_backend.reset();
	}
} // namespace AVPE::NativeLoadTiming
