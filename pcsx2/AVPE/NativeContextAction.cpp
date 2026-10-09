// The right mouse button as the pad's context button. Fork-local.

#include "AVPE/NativeContextAction.h"

#include <lucent/log.h>

namespace AVPE
{
	namespace
	{
		NativeContextAction s_process_context_action;
	} // namespace

	bool NativeContextAction::Push(const bool press)
	{
		std::lock_guard lock(m_mutex);
		if (m_count == m_edges.size())
			return false;
		m_edges[m_count++] = press;
		return true;
	}

	bool NativeContextAction::Press()
	{
		return Push(true);
	}

	bool NativeContextAction::Release()
	{
		return Push(false);
	}

	void NativeContextAction::Step(const u32 input_device, const Guest& guest)
	{
		std::lock_guard lock(m_mutex);
		if (m_count == 0 || !guest.dispatch_idle())
			return;

		const bool press = m_edges[0];
		for (size_t index = 1; index < m_count; ++index)
			m_edges[index - 1] = m_edges[index];
		--m_count;

		const Button* focused = m_focused;
		m_focused = nullptr;
		NativeInputCallbacks::Registry registry;
		if (!NativeInputCallbacks::ReadRegistry(input_device, guest.read, &registry))
		{
			lucent::warn("avpe-context-action", "the input callback registry is unreadable");
			return;
		}
		NativeInputCallbacks::Target target;
		if (press)
		{
			for (const Button& button : Buttons)
			{
				if (NativeInputCallbacks::FindRegistered(
						registry.entries, registry.count, button.vtable, button.focus, &target, guest.read))
				{
					if (guest.queue(target))
						m_focused = &button;
					else
						lucent::warn("avpe-context-action", "focus key of {:08x} was not queued", target.object);
					return;
				}
			}
			return;
		}
		if (focused == nullptr)
			return;
		if (!NativeInputCallbacks::FindRegistered(
				registry.entries, registry.count, focused->vtable, focused->hot, &target, guest.read) ||
			!guest.queue(target))
		{
			lucent::warn("avpe-context-action", "the focused context button's hotkey is not registered once");
		}
	}

	void NativeContextAction::Reset()
	{
		std::lock_guard lock(m_mutex);
		m_count = 0;
		m_focused = nullptr;
	}

	NativeContextAction& NativeContextAction::Process()
	{
		return s_process_context_action;
	}
} // namespace AVPE
