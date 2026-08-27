// AVP:E native cdvdman completion tokens. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeCdvdCompletion.h"

#include <array>
#include <limits>
#include <mutex>

namespace AVPE::NativeCdvdCompletion
{
	namespace
	{
		struct Token
		{
			bool active = false;
			std::uint32_t stack = 0;
			std::int32_t error_code = 0;
		};

		std::mutex s_mutex;
		std::array<Token, Capacity> s_tokens;
		Snapshot s_snapshot;

		void Increment(std::uint64_t* const counter)
		{
			if (*counter != std::numeric_limits<std::uint64_t>::max())
				++*counter;
		}
	} // namespace

	bool Record(const std::uint32_t stack, const std::int32_t error_code)
	{
		std::lock_guard lock(s_mutex);
		for (Token& token : s_tokens)
		{
			if (token.active && token.stack == stack)
			{
				token.error_code = error_code;
				Increment(&s_snapshot.recorded);
				return true;
			}
		}

		for (Token& token : s_tokens)
		{
			if (!token.active)
			{
				token = {.active = true, .stack = stack, .error_code = error_code};
				Increment(&s_snapshot.recorded);
				++s_snapshot.active_tokens;
				return true;
			}
		}

		Increment(&s_snapshot.rejected_records);
		return false;
	}

	std::optional<std::int32_t> Consume(const std::uint32_t stack)
	{
		std::lock_guard lock(s_mutex);
		for (Token& token : s_tokens)
		{
			if (token.active && token.stack == stack)
			{
				const std::int32_t error_code = token.error_code;
				token = {};
				Increment(&s_snapshot.consumed);
				--s_snapshot.active_tokens;
				return error_code;
			}
		}

		Increment(&s_snapshot.consume_misses);
		return std::nullopt;
	}

	Snapshot GetSnapshot()
	{
		std::lock_guard lock(s_mutex);
		return s_snapshot;
	}

	void Reset()
	{
		std::lock_guard lock(s_mutex);
		s_tokens = {};
		s_snapshot = {};
	}
} // namespace AVPE::NativeCdvdCompletion
