// Per-frame placement of AVP:E's drawn PS2 button glyphs. Fork-local.

#pragma once

#include "AVPE/NativePromptGlyphs.h"
#include "AVPE/NativePromptOverlay.h"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace AVPE
{
	// Collects the screen rect of every glyph mesh the guest draws in a frame and
	// publishes them, placed where VU1 rasterises them, once the frame's draws end.
	// EE thread only, apart from Capture.
	class NativePromptPlacement final
	{
	public:
		// CMeshWorkspace::GetMatrix's return from GetScreenBoundingBox: $s3 is the
		// workspace, workspace+4 the CRender node, workspace+8 the drawn resource,
		// [sp+0x60] minX, minY, maxX, maxY.
		static inline constexpr u32 GetMatrixRectReturnPc = 0x00136320;
		static inline constexpr u32 RectStackOffset = 0x60;
		static inline constexpr u32 WorkspaceRenderOffset = 0x04;
		static inline constexpr u32 WorkspaceResourceOffset = 0x08;
		// A glyph's CRender node is embedded in its GMenuItem.
		static inline constexpr u32 ItemRenderOffset = 0x70;
		static inline constexpr u32 ItemHotKeyOffset = 0x118;
		static inline constexpr u32 ItemTextOffset = 0x148;
		static inline constexpr size_t MaxLabelLength = 48;
		// EndFrame's VIF1 kick, after it has written every ViewportData slot.
		static inline constexpr u32 FrameKickPc = 0x001791AC;

		static inline constexpr u32 CurrentWindowAddress = 0x003C6680;
		static inline constexpr u32 WindowDataBase = 0x003C66A0;
		static inline constexpr u32 WindowDataStride = 0x210;
		static inline constexpr u32 WindowCentreXOffset = 0x1D0;
		static inline constexpr u32 WindowCentreYOffset = 0x1D8;
		static inline constexpr u32 WindowCount = 9;
		// ViewportData slot w, uploaded to VU1 as qwords 12+6w..; qword 5 is the GS
		// draw origin _$comp_verts adds after the perspective divide.
		static inline constexpr u32 ViewportDataBase = 0x002CEBD0;
		static inline constexpr u32 ViewportDataStride = 0x60;
		static inline constexpr u32 SlotDrawOriginOffset = 0x50;
		// Slot 2 carries the packed scissor table, so window 2 has no draw origin.
		static inline constexpr u32 ScissorWindow = 2;
		// CRendAPI::GetResolution's RECT {0, 0, width, height}.
		static inline constexpr u32 ResolutionWidthAddress = 0x003C9FE8;
		static inline constexpr u32 ResolutionHeightAddress = 0x003C9FEC;
		// sceGsSetDefDrawEnv centres the framebuffer at 2048 in GS primitive space.
		static inline constexpr float GsPrimitiveCentre = 2048.0f;
		static inline constexpr size_t MaxPrompts = 16;

		using GuestReader = bool (*)(u32 address, void* destination, u32 size);
		using Publisher = std::function<void(PromptFrame)>;

		struct DrawOffset
		{
			float x = 0.0f;
			float y = 0.0f;
		};

		// How far VU1 places a vertex from where TransformPoint does: the slot's draw
		// origin less the window centre and the GS framebuffer origin.
		static DrawOffset ComputeDrawOffset(float origin_x, float origin_y, float centre_x, float centre_y,
			float framebuffer_width, float framebuffer_height);

		explicit NativePromptPlacement(Publisher publisher);

		void ObserveGetMatrixRect(u32 workspace, u32 sp, GuestReader read);
		void ObserveFrameKick(GuestReader read);
		void Reset();
		PromptFrame Capture() const;

		static NativePromptPlacement& Process();
		static bool ShouldInstrumentEePc(u32 pc);

	private:
		struct Pending
		{
			PromptButton button;
			u32 item;
			u32 hotkey;
			std::string label;
			u32 window;
			s32 xmin;
			s32 ymin;
			s32 xmax;
			s32 ymax;
		};

		static std::string ReadLabel(u32 text, GuestReader read);
		std::optional<DrawOffset> ReadDrawOffset(u32 window, float framebuffer_width, float framebuffer_height,
			GuestReader read) const;

		Publisher m_publisher;
		NativePromptGlyphs m_glyphs;
		std::vector<Pending> m_pending;
		mutable std::mutex m_last_mutex;
		PromptFrame m_last;
	};
} // namespace AVPE
