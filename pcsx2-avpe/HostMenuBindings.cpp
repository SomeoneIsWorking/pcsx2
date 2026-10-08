// AVPE product-host menu key bindings. Fork-local; not for upstream PCSX2.
#include "pcsx2-avpe/HostMenuBindings.h"

#include "AVPE/NativeKeyLabels.h"

#include <string>

namespace AVPE::HostMenuBindings
{
	void PublishPromptLabels()
	{
		for (size_t index = 0; index < NativeMenuInput::ActionCount; index++)
		{
			const auto action = static_cast<NativeMenuInput::Action>(index);
			NativeKeyLabels::Process().Set(action, std::string(PromptLabel(action)));
		}
	}
} // namespace AVPE::HostMenuBindings
