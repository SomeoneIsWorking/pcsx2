// AVP:E native cdvdman completion tokens. Fork-local; not for upstream PCSX2.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace AVPE::NativeCdvdCompletion
{
	constexpr std::size_t Capacity = 16;

	struct Snapshot
	{
		std::uint64_t recorded = 0;
		std::uint64_t consumed = 0;
		std::uint64_t consume_misses = 0;
		std::uint64_t rejected_records = 0;
		std::size_t active_tokens = 0;
	};

	// A stack address identifies the caller which must later receive this
	// completion. Recording the same caller replaces its still-pending result.
	bool Record(std::uint32_t stack, std::int32_t error_code);
	std::optional<std::int32_t> Consume(std::uint32_t stack);
	Snapshot GetSnapshot();
	void Reset();
} // namespace AVPE::NativeCdvdCompletion
