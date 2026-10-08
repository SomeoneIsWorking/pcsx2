// The PC key names the host binds to AVP:E menu actions. Fork-local.

#include "AVPE/NativeKeyLabels.h"

namespace AVPE
{
	namespace
	{
		NativeKeyLabels s_process_labels;
	} // namespace

	void NativeKeyLabels::Set(const NativeMenuInput::Action action, std::string label)
	{
		std::scoped_lock lock(m_mutex);
		m_labels[static_cast<size_t>(action)] = std::move(label);
	}

	std::string NativeKeyLabels::Get(const NativeMenuInput::Action action) const
	{
		std::scoped_lock lock(m_mutex);
		return m_labels[static_cast<size_t>(action)];
	}

	NativeKeyLabels& NativeKeyLabels::Process()
	{
		return s_process_labels;
	}
} // namespace AVPE
