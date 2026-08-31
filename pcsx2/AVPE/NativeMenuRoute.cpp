// AVP:E native menu control routes. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeMenuRoute.h"

#include "AVPE/EECallShuttle.h"
#include "AVPE/HttpJson.h"
#include "AVPE/NativeMenuInput.h"

#include <lucent/log.h>

#include <cstdio>

namespace AVPE::NativeMenuRoute
{
	namespace
	{
		int FailureStatus(const NativeMenuInput::Result& result)
		{
			if (result.shuttle_status == EECallShuttle::Status::Busy)
				return 409;
			switch (result.status)
			{
				case NativeMenuInput::Status::MenuUnavailable:
				case NativeMenuInput::Status::AmbiguousMenu:
				case NativeMenuInput::Status::FocusUnavailable:
					return 409;
				default:
					return 500;
			}
		}
	} // namespace

	lucent::http::Response HandleAction(const std::string& body)
	{
		const auto action_name = HttpJson::StringField(body, "action");
		if (!action_name)
			return lucent::http::Response::text(400, "Bad Request", "need action\n");

		NativeMenuInput::Action action;
		if (*action_name == "up")
			action = NativeMenuInput::Action::Up;
		else if (*action_name == "down")
			action = NativeMenuInput::Action::Down;
		else if (*action_name == "left")
			action = NativeMenuInput::Action::Left;
		else if (*action_name == "right")
			action = NativeMenuInput::Action::Right;
		else if (*action_name == "activate")
			action = NativeMenuInput::Action::Activate;
		else if (*action_name == "cancel")
			action = NativeMenuInput::Action::Cancel;
		else
			return lucent::http::Response::text(
				400, "Bad Request", "action must be up, down, left, right, activate, or cancel\n");

		const NativeMenuInput::Result result = NativeMenuInput::Apply(action);
		if (!result.Succeeded())
		{
			lucent::error("avpe-input", "menu {} failed: {}", *action_name, result.error);
			char response[384];
			std::snprintf(response, sizeof(response),
				"%s\nstopped_pc=0x%08X last_avpe_text_pc=0x%08X elapsed_cycles=%llu "
				"stack_restored=%s\n",
				result.error, result.stopped_pc, result.last_avpe_text_pc,
				static_cast<unsigned long long>(result.elapsed_cycles),
				result.stack_restored ? "true" : "false");
			return lucent::http::Response::text(FailureStatus(result), "Native Menu Input Failed",
				response);
		}

		char response[960];
		std::snprintf(response, sizeof(response),
			R"({"action":"%s","source":"%s","menu":"0x%08X","menu_vtable":"0x%08X","handler":"0x%08X","action_target":"0x%08X","focused_item_action":"0x%08X","focused_item_action_valid":%s,"callback_count":%u,"before":{"focus_handle":"0x%08X","focus_object":"0x%08X","focus_vtable":"0x%08X"},"after":{"focus_handle":"0x%08X","focus_object":"0x%08X","focus_vtable":"0x%08X"},"execution":"%s","stopped_pc":"0x%08X","last_avpe_text_pc":"0x%08X","stack_restored":%s,"elapsed_cycles":%llu,"deferred":%s,"deferred_call_id":%llu})",
			action_name->c_str(), NativeMenuInput::SourceName(result.source), result.menu, result.menu_vtable, result.handler,
			result.action_target, result.focused_item_action, result.focused_item_action_valid ? "true" : "false", result.callback_count, result.before.handle, result.before.object, result.before.vtable, result.after.handle,
			result.after.object, result.after.vtable, result.deferred ? "deferred" : "synchronous", result.stopped_pc, result.last_avpe_text_pc,
			result.stack_restored ? "true" : "false", static_cast<unsigned long long>(result.elapsed_cycles),
			result.deferred ? "true" : "false", static_cast<unsigned long long>(result.deferred_call_id));
		return lucent::http::Response::json(
			result.deferred ? 202 : 200, result.deferred ? "Accepted" : "OK", response);
	}

	lucent::http::Response HandleState()
	{
		const NativeMenuInput::Result result = NativeMenuInput::Inspect();
		char response[448];
		std::snprintf(response, sizeof(response),
			R"({"source":"%s","menu":"0x%08X","menu_vtable":"0x%08X","conflicting_menu":"0x%08X","conflicting_menu_vtable":"0x%08X","action_target":"0x%08X","focused_item_action":"0x%08X","focused_item_action_valid":%s,"callback_count":%u,"focus_handle":"0x%08X","focus_object":"0x%08X","focus_vtable":"0x%08X"})",
			NativeMenuInput::SourceName(result.source), result.menu, result.menu_vtable, result.conflicting_menu, result.conflicting_menu_vtable, result.action_target, result.focused_item_action,
			result.focused_item_action_valid ? "true" : "false", result.callback_count, result.before.handle, result.before.object, result.before.vtable);
		if (result.status == NativeMenuInput::Status::AmbiguousMenu)
			return lucent::http::Response::json(FailureStatus(result), "Native Menu State Ambiguous", response);
		if (!result.Succeeded())
		{
			return lucent::http::Response::text(FailureStatus(result), "Native Menu State Unavailable",
				std::string(result.error) + "\n");
		}
		return lucent::http::Response::json(200, "OK", response);
	}
} // namespace AVPE::NativeMenuRoute
