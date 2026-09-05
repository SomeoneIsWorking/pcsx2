// Grounded EALOGO movie service-census boundary. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMovieBiosBoundary.h"

#include "AVPE/AVPE.h"
#include "AVPE/NativeBiosTrace.h"
#include "AVPE/NativeConfig.h"

#include <condition_variable>
#include <mutex>

namespace AVPE::NativeMovieBiosBoundary
{
	namespace
	{
		constexpr std::string_view MoviePath = "MOVIES/EALOGO.PSS";

		enum class State
		{
			Idle,
			Capturing,
			AwaitingCloseImport,
			Complete,
		};

		std::mutex s_mutex;
		std::condition_variable s_completion;
		State s_state = State::Idle;
		std::string s_capture;

		bool IsRequested()
		{
			return IsSurfacelessControlTest() && NativeConfig::BiosMovieTraceRequested();
		}

		std::string AppendBoundary(std::string snapshot, const bool complete)
		{
			if (snapshot.empty() || snapshot.back() != '}')
				return {};
			snapshot.pop_back();
			snapshot += ",\"movie_boundary\":{\"path\":\"MOVIES/EALOGO.PSS\",\"complete\":";
			snapshot += complete ? "true" : "false";
			snapshot += "}}";
			return snapshot;
		}
	} // namespace

	void Reset()
	{
		std::lock_guard lock(s_mutex);
		s_state = State::Idle;
		s_capture.clear();
		s_completion.notify_all();
	}

	void ObserveNativeOpen(const std::string_view canonical_path)
	{
		if (canonical_path != MoviePath || !IsRequested())
			return;
		{
			std::lock_guard lock(s_mutex);
			if (s_state != State::Idle)
				return;
			s_state = State::Capturing;
			s_capture.clear();
		}
		NativeBiosTrace::SetEnabled(true);
	}

	void ObserveNativeClose(const std::string_view canonical_path)
	{
		if (canonical_path != MoviePath)
			return;
		std::lock_guard lock(s_mutex);
		if (s_state == State::Capturing)
			s_state = State::AwaitingCloseImport;
	}

	void ObserveHandledIopImport(const std::string_view library, const std::string_view function)
	{
		if (library != "ioman" || function != "close")
			return;
		{
			std::lock_guard lock(s_mutex);
			if (s_state != State::AwaitingCloseImport)
				return;
			s_capture = AppendBoundary(NativeBiosTrace::SnapshotAndDisableJson(), true);
			s_state = State::Complete;
		}
		s_completion.notify_all();
	}

	std::string CaptureJson(const std::chrono::milliseconds timeout)
	{
		std::unique_lock lock(s_mutex);
		if (s_state != State::Complete)
			s_completion.wait_for(lock, timeout, []() { return s_state == State::Complete; });
		if (s_state == State::Complete)
			return s_capture;
		s_state = State::Idle;
		lock.unlock();
		return AppendBoundary(NativeBiosTrace::SnapshotAndDisableJson(), false);
	}
} // namespace AVPE::NativeMovieBiosBoundary
