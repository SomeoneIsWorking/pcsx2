// AVP:E bounded IOP oracle-return site registry. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeIopReturnSites.h"

#include <array>
#include <atomic>
#include <mutex>

namespace AVPE::NativeIopReturnSites
{
	namespace
	{
		constexpr u32 MaximumSites = 256;
		constexpr u32 InstructionAlignment = 4;
		std::array<std::atomic<u32>, MaximumSites> s_sites{};
		std::atomic<u32> s_site_count{0};
		std::mutex s_registration_mutex;
	} // namespace

	Registration Register(const u32 pc)
	{
		if (pc == 0 || pc % InstructionAlignment != 0)
			return Registration::Full;
		std::lock_guard lock(s_registration_mutex);
		const u32 site_count = s_site_count.load(std::memory_order_relaxed);
		for (u32 index = 0; index < site_count; ++index)
		{
			if (s_sites[index].load(std::memory_order_relaxed) == pc)
				return Registration::Existing;
		}
		if (site_count >= MaximumSites)
			return Registration::Full;
		s_sites[site_count].store(pc, std::memory_order_relaxed);
		s_site_count.store(site_count + 1, std::memory_order_release);
		return Registration::Added;
	}

	bool Contains(const u32 pc)
	{
		if (pc == 0)
			return false;
		const u32 site_count = s_site_count.load(std::memory_order_acquire);
		for (u32 index = 0; index < site_count; ++index)
		{
			if (s_sites[index].load(std::memory_order_relaxed) == pc)
				return true;
		}
		return false;
	}
} // namespace AVPE::NativeIopReturnSites
