// The PC key names the host binds to AVP:E menu actions. Fork-local.

#pragma once

#include "AVPE/NativeMenuInput.h"

#include <array>
#include <mutex>
#include <string>

namespace AVPE
{
	// Written once by the host's binding owner; read by the prompt overlay and the
	// title text from other threads.
	class NativeKeyLabels final
	{
	public:
		void Set(NativeMenuInput::Action action, std::string label);
		std::string Get(NativeMenuInput::Action action) const;

		static NativeKeyLabels& Process();

	private:
		mutable std::mutex m_mutex;
		std::array<std::string, NativeMenuInput::ActionCount> m_labels;
	};
} // namespace AVPE
