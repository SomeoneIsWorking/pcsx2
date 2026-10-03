// Bounded observation of AVP:E's live CRender draw dispatch and of the
// screen-space AABB the drawing workspace computes. Fork-local.

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
		m_dispatches.reserve(MaxAdmittedResources);
		m_samples.reserve(MaxSamples);
	}

	void NativeMeshBoundsTrace::Start(const std::span<const u32> resources)
	{
		std::scoped_lock lock(m_mutex);
		m_armed.store(false, std::memory_order_release);
		ClearUnderLock();
		// A null address is never a real resource, so admitting one would make a
		// zeroed dispatch register look like a match.
		for (const u32 resource : resources)
		{
			if (resource == 0 || m_admitted_count >= MaxAdmittedResources)
			{
				continue;
			}
			m_admitted[m_admitted_count++] = resource;
		}
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
		m_admitted_count = 0;
		m_admitted.fill(0);
		m_observed_dispatches = 0;
		m_matched_dispatches = 0;
		m_observed_rects = 0;
		m_matched_rects = 0;
		m_invalid_dispatches = 0;
		m_invalid_reads = 0;
		m_dropped_samples = 0;
		m_dispatches.clear();
		m_samples.clear();
	}

	bool NativeMeshBoundsTrace::ShouldInstrumentEePc(const u32 pc)
	{
		// The recompiler decides where to split a block when it compiles it, so
		// this must match on the PC alone. Gating it on the armed flag would let
		// blocks compiled while disarmed run the dispatch with no call-out.
		return pc == RenderDisplayDispatchPc || pc == GetMatrixRectReturnPc;
	}

	NativeMeshBoundsTrace& NativeMeshBoundsTrace::Process()
	{
		return s_process_trace;
	}

	bool NativeMeshBoundsTrace::AdmitsUnderLock(const u32 resource) const
	{
		return std::find(m_admitted.begin(), m_admitted.begin() + static_cast<std::ptrdiff_t>(m_admitted_count),
				   resource) != m_admitted.begin() + static_cast<std::ptrdiff_t>(m_admitted_count);
	}

	void NativeMeshBoundsTrace::ObserveRenderDispatch(const u32 resource, const u32 render_node, const u32 render,
		const GuestReader read)
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
		++m_observed_dispatches;
		if (!AdmitsUnderLock(resource))
		{
			return;
		}
		++m_matched_dispatches;
		if (read == nullptr || !GuestObjects::IsPlausibleAddress(render_node) ||
			!GuestObjects::IsPlausibleAddress(render))
		{
			++m_invalid_dispatches;
			return;
		}

		// The owning workspace and the GetMatrix its vtable resolves to say which
		// workspace class draws this resource, and so whether a rect is produced.
		u32 workspace = 0;
		u32 workspace_vtable = 0;
		u32 get_matrix = 0;
		if (!ReadU32(read, render_node + 0x20, &workspace) || !GuestObjects::IsPlausibleAddress(workspace) ||
			!ReadU32(read, workspace, &workspace_vtable) ||
			!GuestObjects::IsPlausibleAddress(workspace_vtable) ||
			!ReadU32(read, workspace_vtable + 0x14, &get_matrix))
		{
			++m_invalid_dispatches;
			return;
		}

		auto entry = std::find_if(m_dispatches.begin(), m_dispatches.end(),
			[&](const Dispatch& candidate) { return candidate.resource == resource; });
		if (entry != m_dispatches.end())
		{
			++entry->calls;
			return;
		}
		if (m_dispatches.size() >= MaxAdmittedResources)
		{
			return;
		}
		m_dispatches.push_back(Dispatch{
			.resource = resource,
			.render_node = render_node,
			.render = render,
			.workspace = workspace,
			.get_matrix = get_matrix,
			.calls = 1,
		});
	}

	void NativeMeshBoundsTrace::ObserveGetMatrixRect(const u32 workspace, const u32 sp, const GuestReader read)
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
		++m_observed_rects;

		s32 xmin = 0;
		s32 ymin = 0;
		s32 xmax = 0;
		s32 ymax = 0;
		u32 render = 0;
		u32 bounds_object = 0;
		if (read == nullptr || !GuestObjects::IsPlausibleAddress(workspace) ||
			!GuestObjects::IsPlausibleAddress(sp) ||
			!ReadU32(read, workspace + 0x08, &bounds_object))
		{
			++m_invalid_reads;
			return;
		}
		if (!AdmitsUnderLock(bounds_object))
		{
			return;
		}
		++m_matched_rects;
		if (!ReadS32(read, sp + RectStackOffset + 0x00, &xmin) ||
			!ReadS32(read, sp + RectStackOffset + 0x04, &ymin) ||
			!ReadS32(read, sp + RectStackOffset + 0x08, &xmax) ||
			!ReadS32(read, sp + RectStackOffset + 0x0C, &ymax) ||
			!ReadU32(read, workspace + 0x04, &render) || !GuestObjects::IsPlausibleAddress(render))
		{
			++m_invalid_reads;
			return;
		}

		auto sample = std::find_if(m_samples.begin(), m_samples.end(), [&](const Sample& candidate) {
			return candidate.workspace == workspace && candidate.render == render &&
			       candidate.bounds_object == bounds_object && candidate.xmin == xmin &&
			       candidate.ymin == ymin && candidate.xmax == xmax && candidate.ymax == ymax;
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
				.xmin = xmin,
				.ymin = ymin,
				.xmax = xmax,
				.ymax = ymax,
				.calls = 1,
			});
			return;
		}
		++sample->calls;
	}

	NativeMeshBoundsTrace::Snapshot NativeMeshBoundsTrace::Capture() const
	{
		std::scoped_lock lock(m_mutex);
		Snapshot snapshot{
			.armed = m_armed.load(std::memory_order_acquire),
			.observed_dispatches = m_observed_dispatches,
			.matched_dispatches = m_matched_dispatches,
			.observed_rects = m_observed_rects,
			.matched_rects = m_matched_rects,
			.invalid_dispatches = m_invalid_dispatches,
			.invalid_reads = m_invalid_reads,
			.dropped_samples = m_dropped_samples,
			.dispatches = m_dispatches,
			.samples = m_samples,
		};
		snapshot.admitted.assign(m_admitted.begin(),
			m_admitted.begin() + static_cast<std::ptrdiff_t>(m_admitted_count));
		return snapshot;
	}
} // namespace AVPE