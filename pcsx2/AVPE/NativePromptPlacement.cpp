// Per-frame placement of AVP:E's drawn PS2 button glyphs. Fork-local.

#include "AVPE/NativePromptPlacement.h"

#include "AVPE/GuestObjects.h"
#include "MTGS.h"

#include <cstring>
#include <utility>

namespace AVPE
{
	namespace
	{
		NativePromptPlacement s_process_placement([](PromptFrame frame) {
			MTGS::RunOnGSThread(
				[frame = std::move(frame)]() mutable { NativePromptOverlay::Process().Publish(std::move(frame)); });
		});

		template <typename T>
		bool ReadValue(const NativePromptPlacement::GuestReader read, const u32 address, T* const value)
		{
			std::array<u8, sizeof(T)> bytes{};
			if (!read(address, bytes.data(), static_cast<u32>(bytes.size())))
			{
				return false;
			}
			std::memcpy(value, bytes.data(), sizeof(T));
			return true;
		}
	} // namespace

	NativePromptPlacement::NativePromptPlacement(Publisher publisher)
		: m_publisher(std::move(publisher))
	{
		m_pending.reserve(MaxPrompts);
	}

	NativePromptPlacement::DrawOffset NativePromptPlacement::ComputeDrawOffset(const float origin_x,
		const float origin_y, const float centre_x, const float centre_y, const float framebuffer_width,
		const float framebuffer_height)
	{
		return {
			origin_x - centre_x - (GsPrimitiveCentre - framebuffer_width * 0.5f),
			origin_y - centre_y - (GsPrimitiveCentre - framebuffer_height * 0.5f),
		};
	}

	bool NativePromptPlacement::ShouldInstrumentEePc(const u32 pc)
	{
		return pc == GetMatrixRectReturnPc || pc == FrameKickPc;
	}

	NativePromptPlacement& NativePromptPlacement::Process()
	{
		return s_process_placement;
	}

	void NativePromptPlacement::ObserveGetMatrixRect(const u32 workspace, const u32 sp, const GuestReader read)
	{
		u32 resource = 0;
		if (m_pending.size() >= MaxPrompts || !GuestObjects::IsPlausibleAddress(workspace) ||
			!ReadValue(read, workspace + WorkspaceResourceOffset, &resource))
		{
			return;
		}
		const std::optional<PromptButton> button = m_glyphs.ButtonFor(resource);
		if (!button)
		{
			return;
		}
		const std::optional<NativeMenuInput::Action> action = NativePromptGlyphs::ActionFor(*button);
		if (!action)
		{
			return;
		}
		Pending pending{*action, 0, 0, 0, 0, 0};
		if (!ReadValue(read, CurrentWindowAddress, &pending.window) ||
			!ReadValue(read, sp + RectStackOffset + 0x00, &pending.xmin) ||
			!ReadValue(read, sp + RectStackOffset + 0x04, &pending.ymin) ||
			!ReadValue(read, sp + RectStackOffset + 0x08, &pending.xmax) ||
			!ReadValue(read, sp + RectStackOffset + 0x0C, &pending.ymax))
		{
			return;
		}
		m_pending.push_back(pending);
	}

	std::optional<NativePromptPlacement::DrawOffset> NativePromptPlacement::ReadDrawOffset(const u32 window,
		const float framebuffer_width, const float framebuffer_height, const GuestReader read) const
	{
		if (window >= WindowCount || window == ScissorWindow)
		{
			return std::nullopt;
		}
		const u32 slot = ViewportDataBase + window * ViewportDataStride + SlotDrawOriginOffset;
		const u32 window_data = WindowDataBase + window * WindowDataStride;
		float origin_x = 0.0f;
		float origin_y = 0.0f;
		float centre_x = 0.0f;
		float centre_y = 0.0f;
		if (!ReadValue(read, slot, &origin_x) || !ReadValue(read, slot + 4, &origin_y) ||
			!ReadValue(read, window_data + WindowCentreXOffset, &centre_x) ||
			!ReadValue(read, window_data + WindowCentreYOffset, &centre_y))
		{
			return std::nullopt;
		}
		return ComputeDrawOffset(origin_x, origin_y, centre_x, centre_y, framebuffer_width, framebuffer_height);
	}

	void NativePromptPlacement::ObserveFrameKick(const GuestReader read)
	{
		PromptFrame frame;
		s32 width = 0;
		s32 height = 0;
		if (ReadValue(read, ResolutionWidthAddress, &width) && ReadValue(read, ResolutionHeightAddress, &height) &&
			width > 0 && height > 0)
		{
			frame.framebuffer_width = static_cast<float>(width);
			frame.framebuffer_height = static_cast<float>(height);
			for (const Pending& pending : m_pending)
			{
				const std::optional<DrawOffset> offset =
					ReadDrawOffset(pending.window, frame.framebuffer_width, frame.framebuffer_height, read);
				if (!offset)
				{
					continue;
				}
				frame.prompts.push_back({pending.action, static_cast<float>(pending.xmin) + offset->x,
					static_cast<float>(pending.ymin) + offset->y, static_cast<float>(pending.xmax) + offset->x,
					static_cast<float>(pending.ymax) + offset->y});
			}
		}
		m_pending.clear();
		// The glyph meshes live in MASTER.TBD, which a load can replace between frames.
		m_glyphs.Resolve(read);
		{
			std::scoped_lock lock(m_last_mutex);
			m_last = frame;
		}
		m_publisher(std::move(frame));
	}

	void NativePromptPlacement::Reset()
	{
		m_pending.clear();
		m_glyphs = {};
		std::scoped_lock lock(m_last_mutex);
		m_last = {};
	}

	PromptFrame NativePromptPlacement::Capture() const
	{
		std::scoped_lock lock(m_last_mutex);
		return m_last;
	}
} // namespace AVPE
