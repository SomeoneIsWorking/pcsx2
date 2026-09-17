// Bounded observation of AVP:E's live CMeshWorkspace::GetMatrix screen-space
// AABB outputs. Fork-local.
#pragma once

#include "common/Pcsx2Defs.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

namespace AVPE
{
	// CMeshWorkspace::GetMatrix (0x001362c0) calls a vtable slot at
	// `*(workspace+8)+0x20` with `(*(workspace+8), workspace+0x10, &iStack_20)`,
	// writing a four-int screen-space AABB (xmax, ymax, xmin, ymin, per the
	// immediately following cull test against GetResolution()) into the
	// caller's stack frame at [sp+0x60..sp+0x6c]. The vtable call is
	// `jalr t9` at 0x00136318 with delay slot `addiu a2,sp,0x60`; its return
	// address, where $s3 still holds the CMeshWorkspace `this` pointer
	// (param_1) and the four ints are live on the stack, is 0x00136320.
	class NativeMeshBoundsTrace final
	{
	public:
		static inline constexpr u32 GetMatrixRectReturnPc = 0x00136320;
		static inline constexpr u32 RectStackOffset = 0x60;
		static inline constexpr size_t MaxSamples = 64;

		using GuestReader = bool (*)(u32 address, void* destination, u32 size);

		struct Sample
		{
			u32 workspace = 0; // CMeshWorkspace `this` (param_1).
			u32 render = 0; // CRender* at workspace+4.
			u32 bounds_object = 0; // Second object at workspace+8 whose vtable+0x20 produced the rect.
			s32 xmax = 0;
			s32 ymax = 0;
			s32 xmin = 0;
			s32 ymin = 0;
			u64 calls = 0;
		};

		struct Snapshot
		{
			bool armed = false;
			u64 observed_calls = 0;
			u64 invalid_reads = 0;
			u64 dropped_samples = 0;
			std::vector<Sample> samples;
		};

		NativeMeshBoundsTrace();

		void Start();
		void Stop();
		void Reset();
		void Observe(u32 workspace, u32 sp, GuestReader read);
		Snapshot Capture() const;

		static NativeMeshBoundsTrace& Process();
		static bool ShouldInstrumentEePc(u32 pc);

	private:
		void ClearUnderLock();

		mutable std::mutex m_mutex;
		std::atomic_bool m_armed{false};
		u64 m_observed_calls = 0;
		u64 m_invalid_reads = 0;
		u64 m_dropped_samples = 0;
		std::vector<Sample> m_samples;
	};
} // namespace AVPE
