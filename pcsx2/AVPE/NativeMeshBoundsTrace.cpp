// Bounded observation of AVP:E's live CMeshWorkspace::GetMatrix screen-space
// AABB outputs. Fork-local.

#include "AVPE/NativeMeshBoundsTrace.h"

#include "AVPE/GuestObjects.h"

#include <algorithm>
#include <array>

namespace AVPE
{
	namespace
	{
		NativeMeshBoundsTrace s_process_trace;

		bool ReadS32(const NativeMeshBoundsTrace::GuestReader read, const u32 address, s32* const value)
		{
			std::array<u8, 4> bytes{};
			if (!read(address, bytes.data(), static_cast<u32>(bytes.size())))
				return false;
			*value = static_cast<s32>(static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
									  (static_cast<u32>(bytes[2]) << 16) | (static_cast<u32>(bytes[3]) << 24));
			return true;
		}

		bool ReadU32(const NativeMeshBoundsTrace::GuestReader read, const u32 address, u32* const value)
		{
			std::array<u8, 4> bytes{};
			if (!read(address, bytes.data(), static_cast<u32>(bytes.size())))
				return false;
			*value = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
			         (static_cast<u32>(bytes[2]) << 16) | (static_cast<u32>(bytes[3]) << 24);
			return true;
		}
	} // namespace

	NativeMeshBoundsTrace::NativeMeshBoundsTrace()
	{
		m_samples.reserve(MaxSamples);
	}

	void NativeMeshBoundsTrace::Start()
	{
		std::scoped_lock lock(m_mutex);
		m_armed.store(false, std::memory_order_release);
		ClearUnderLock();
		m_armed.store(true, std::memory_order_release);
	}

	void NativeMeshBoundsTrace::Stop()
	{
		std::scoped_lock lock(m_mutex);
		m_armed.store(false, std::memory_order_release);
	}

	void NativeMeshBoundsTrace::Reset()
	{
		std::scoped_lock lock(m_mutex);
		m_armed.store(false, std::memory_order_release);
		ClearUnderLock();
	}

	void NativeMeshBoundsTrace::ClearUnderLock()
	{
		m_observed_calls = 0;
		m_invalid_reads = 0;
		m_dropped_samples = 0;
		m_samples.clear();
	}

	bool NativeMeshBoundsTrace::ShouldInstrumentEePc(const u32 pc)
	{
		return pc == GetMatrixRectReturnPc;
	}

	NativeMeshBoundsTrace& NativeMeshBoundsTrace::Process()
	{
		return s_process_trace;
	}

	void NativeMeshBoundsTrace::Observe(const u32 workspace, const u32 sp, const GuestReader read)
	{
		if (!m_armed.load(std::memory_order_acquire))
		{
			return;
		}
		std::scoped_lock lock(m_mutex);
		if (!m_armed.load(std::memory_order_acquire))
		{
			return;
		}
		++m_observed_calls;

		s32 xmax = 0;
		s32 ymax = 0;
		s32 xmin = 0;
		s32 ymin = 0;
		u32 render = 0;
		u32 bounds_object = 0;
		if (read == nullptr || !GuestObjects::IsPlausibleAddress(workspace) ||
			!GuestObjects::IsPlausibleAddress(sp) ||
			!ReadS32(read, sp + RectStackOffset + 0x00, &xmax) ||
			!ReadS32(read, sp + RectStackOffset + 0x04, &ymax) ||
			!ReadS32(read, sp + RectStackOffset + 0x08, &xmin) ||
			!ReadS32(read, sp + RectStackOffset + 0x0C, &ymin) ||
			!ReadU32(read, workspace + 0x04, &render) || !GuestObjects::IsPlausibleAddress(render) ||
			!ReadU32(read, workspace + 0x08, &bounds_object) ||
			!GuestObjects::IsPlausibleAddress(bounds_object))
		{
			++m_invalid_reads;
			return;
		}

		auto sample = std::find_if(m_samples.begin(), m_samples.end(), [&](const Sample& candidate) {
			return candidate.workspace == workspace && candidate.render == render &&
			       candidate.bounds_object == bounds_object && candidate.xmax == xmax &&
			       candidate.ymax == ymax && candidate.xmin == xmin && candidate.ymin == ymin;
		});
		if (sample == m_samples.end())
		{
			if (m_samples.size() >= MaxSamples)
			{
				++m_dropped_samples;
				return;
			}
			m_samples.push_back(Sample{
				.workspace = workspace,
				.render = render,
				.bounds_object = bounds_object,
				.xmax = xmax,
				.ymax = ymax,
				.xmin = xmin,
				.ymin = ymin,
				.calls = 1,
			});
			return;
		}
		++sample->calls;
	}

	NativeMeshBoundsTrace::Snapshot NativeMeshBoundsTrace::Capture() const
	{
		std::scoped_lock lock(m_mutex);
		return Snapshot{
			.armed = m_armed.load(std::memory_order_acquire),
			.observed_calls = m_observed_calls,
			.invalid_reads = m_invalid_reads,
			.dropped_samples = m_dropped_samples,
			.samples = m_samples,
		};
	}
} // namespace AVPE
