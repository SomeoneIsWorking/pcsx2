// Bounded observation of AVP:E's live CRender draw dispatch and of the
// screen-space AABB the drawing workspace computes. Fork-local.

#pragma once

#include "common/Pcsx2Defs.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <span>
#include <vector>

namespace AVPE
{
	// CRender::Display (0x00136fd0) takes a render node's CRendResource at
	// CRender+0x1c, culls it, and dispatches `resource->vtable[0x18]` -- which
	// every CRend* primitive overrides with its own Render -- through `jalr t9`
	// at 0x001370a4, whose delay slot only moves the CRender into $a1. At that
	// instruction $a0 is the CRendResource, $s0 the owning CRender, and $t9 the
	// resolved Render. Every visible draw passes through here; CRender::CoreRender
	// (0x001370f0) dispatches the same slot, but the title never reaches it.
	//
	// One mission frame draws hundreds of distinct resources, so the observer
	// admits only the resources it is explicitly armed with and counts the rest
	// as unadmitted. A zero match count then means the armed resource was not
	// drawn here, rather than that the table filled up first.
	class NativeMeshBoundsTrace final
	{
	public:
		static inline constexpr u32 RenderDisplayDispatchPc = 0x001370A4;

		// CMeshWorkspace::GetMatrix (0x001362c0) calls the CRendResource at
		// workspace+8 through its vtable slot 0x20 with `(*(workspace+8),
		// workspace+0x10, &iStack_20)`, writing the screen-space AABB that
		// GetScreenBoundingBox__13CRendBaseMesh (0x00135af0) produces into the
		// caller's stack frame at [sp+0x60..sp+0x6c]. That routine seeds
		// [0]=[1]=+10000 and [2]=[3]=-10000 then accumulates the transformed
		// bounding-box corners, so the four words are minX, minY, maxX, maxY in
		// that order. The vtable call is `jalr t9` at 0x00136318 with delay slot
		// `addiu a2,sp,0x60`; its return address, where $s3 still holds the
		// CMeshWorkspace `this` pointer (param_1) and the four ints are live on
		// the stack, is 0x00136320.
		static inline constexpr u32 GetMatrixRectReturnPc = 0x00136320;
		static inline constexpr u32 RectStackOffset = 0x60;

		// PS2ProcessVerts (0x00188720) computes a model-space translation that
		// GetScreenBoundingBox never sees. `lwc1 f2,0x54(s0)` and `lwc1 f1,0x58(s0)`
		// load piVar9[0x15] and piVar9[0x16] from the per-primitive parameter
		// record s0 points at; each is added to an _EFFECT_SHELL-only stack term
		// and the sums are left at sp+0xE4 and sp+0xE8 before being stored to
		// packet bytes 0x70 and 0x74 at 0x001892CC and 0x001892D4. The skinned
		// copy repeats the stores at 0x00189588 and 0x00189590, so observing
		// either PC reads the same two stack slots.
		static inline constexpr u32 ProcessVertsTranslatePc = 0x001892D4;
		static inline constexpr u32 ProcessVertsSkinnedTranslatePc = 0x00189590;
		static inline constexpr u32 TranslateStackXOffset = 0xE4;
		static inline constexpr u32 TranslateStackYOffset = 0xE8;
		static inline constexpr size_t MaxSamples = 32;
		static inline constexpr size_t MaxAdmittedResources = 8;

		using GuestReader = bool (*)(u32 address, void* destination, u32 size);

		// One live draw of an admitted CRendResource.
		struct Dispatch
		{
			u32 resource = 0; // CRendResource* ($a0).
			u32 render_node = 0; // Owning CRender* ($s0).
			u32 render = 0; // Resolved resource->vtable[0x18] Render implementation ($t9).
			u32 workspace = 0; // CRendWorkspace* at CRender+0x20.
			u32 get_matrix = 0; // Resolved workspace->vtable[0x14] GetMatrix implementation.
			u64 calls = 0;
		};

		struct Sample
		{
			u32 workspace = 0; // CMeshWorkspace `this` (param_1).
			u32 render = 0; // CRender* at workspace+4.
			u32 bounds_object = 0; // CRendResource at workspace+8 that produced the rect.
			s32 xmin = 0;
			s32 ymin = 0;
			s32 xmax = 0;
			s32 ymax = 0;
			u64 calls = 0;
		};

		struct Translate
		{
			u32 resource = 0; // Armed CRendResource whose draw produced this packet.
			u32 x_bits = 0; // piVar9[0x15] plus the effect term, as raw float bits.
			u32 y_bits = 0; // piVar9[0x16] plus the effect term, as raw float bits.
		};

		struct Snapshot
		{
			bool armed = false;
			std::vector<u32> admitted;
			u64 observed_dispatches = 0;
			u64 matched_dispatches = 0;
			u64 observed_rects = 0;
			u64 matched_rects = 0;
			u64 observed_translates = 0;
			u64 matched_translates = 0;
			u64 invalid_dispatches = 0;
			u64 invalid_reads = 0;
			u64 dropped_samples = 0;
			std::vector<Dispatch> dispatches;
			std::vector<Sample> samples;
			std::vector<Translate> translates;
		};

		NativeMeshBoundsTrace();

		void Start(std::span<const u32> resources);
		void Stop();
		void Reset();
		void ObserveRenderDispatch(u32 resource, u32 render_node, u32 render, GuestReader read);
		void ObserveGetMatrixRect(u32 workspace, u32 sp, GuestReader read);
		void ObserveProcessVertsTranslate(u32 sp, GuestReader read);
		Snapshot Capture() const;

		static NativeMeshBoundsTrace& Process();
		static bool ShouldInstrumentEePc(u32 pc);

	private:
		void ClearUnderLock();
		bool AdmitsUnderLock(u32 resource) const;

		mutable std::mutex m_mutex;
		std::atomic_bool m_armed{false};
		std::array<u32, MaxAdmittedResources> m_admitted{};
		size_t m_admitted_count = 0;
		u64 m_observed_dispatches = 0;
		u64 m_matched_dispatches = 0;
		u64 m_observed_rects = 0;
		u64 m_matched_rects = 0;
		u64 m_observed_translates = 0;
		u64 m_matched_translates = 0;
		u64 m_invalid_dispatches = 0;
		u64 m_invalid_reads = 0;
		u64 m_dropped_samples = 0;
		// Non-zero only between an admitted render dispatch and the vertex
		// packet that dispatch produces, so the translate is attributed to the
		// draw that caused it.
		u32 m_pending_translate_resource = 0;
		std::vector<Dispatch> m_dispatches;
		std::vector<Sample> m_samples;
		std::vector<Translate> m_translates;
	};
} // namespace AVPE