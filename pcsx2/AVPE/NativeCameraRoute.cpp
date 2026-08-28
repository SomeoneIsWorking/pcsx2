// AVPE camera control route serialization. Fork-local; not for upstream PCSX2.

#include "AVPE/NativeCameraRoute.h"

#include "AVPE/EECallShuttle.h"
#include "AVPE/HttpJson.h"
#include "AVPE/NativeCameraInput.h"

#include <lucent/log.h>

#include <cstdio>
#include <optional>

namespace AVPE::NativeCameraRoute
{
	static std::string StateJson(const NativeCameraInput::State& state)
	{
		char response[768];
		std::snprintf(response, sizeof(response),
			R"({"pointer":"0x%08X","pointer_input_type":%u,"camera":"0x%08X","direction":[%.6f,%.6f,%.6f],"move":[%.6f,%.6f],"camera_mode":%s,"minimap":"0x%08X","cursor":[%.6f,%.6f,%.6f],"minimap_camera":[%.6f,%.6f],"minimap_mode":%s,"minimap_pointer_mode":%s})",
			state.pointer, state.pointer_input_type, state.camera,
			state.camera_direction_x, state.camera_direction_y,
			state.camera_direction_z, state.camera_move_x, state.camera_move_y,
			state.camera_mode ? "true" : "false", state.minimap, state.minimap_cursor_x,
			state.minimap_cursor_y, state.minimap_cursor_z, state.minimap_camera_x,
			state.minimap_camera_y, state.minimap_mode ? "true" : "false",
			state.minimap_pointer_mode ? "true" : "false");
		return response;
	}

	static std::optional<NativeCameraInput::Action> ParseAction(const std::string& name)
	{
		if (name == "move")
			return NativeCameraInput::Action::Move;
		if (name == "rotate")
			return NativeCameraInput::Action::Rotate;
		if (name == "zoom")
			return NativeCameraInput::Action::Zoom;
		return std::nullopt;
	}

	lucent::http::Response Handle(const std::string& body)
	{
		const auto action_name = HttpJson::StringField(body, "action");
		const auto x = HttpJson::FloatField(body, "x");
		const auto y = HttpJson::FloatField(body, "y");
		if (!action_name || !x || (action_name != "zoom" && !y))
			return lucent::http::Response::text(400, "Bad Request", "need action and finite x+y (zoom only needs x)\n");

		const std::optional<NativeCameraInput::Action> action = ParseAction(*action_name);
		if (!action.has_value())
			return lucent::http::Response::text(400, "Bad Request", "action must be move, rotate, or zoom\n");

		const NativeCameraInput::Result result = NativeCameraInput::Apply(*action, *x, y.value_or(0.0f));
		if (!result.Succeeded())
		{
			int status = result.shuttle_status == EECallShuttle::Status::Busy ? 409 : 500;
			if (result.status == NativeCameraInput::Status::InvalidInput)
				status = 400;
			else if (result.status == NativeCameraInput::Status::CameraUnavailable)
				status = 409;
			lucent::error("avpe-input", "camera {} failed: {}", *action_name, result.error);
			return lucent::http::Response::text(status, "Native Camera Input Failed",
				std::string(result.error) + "\n");
		}

		char staging[16];
		std::snprintf(staging, sizeof(staging), "%08X", result.staging_address);
		const std::string response = "{\"action\":\"" + *action_name + "\",\"input\":[" +
		                             std::to_string(result.input_x) + "," + std::to_string(result.input_y) +
		                             "],\"before\":" + StateJson(result.before) +
		                             ",\"after\":" + StateJson(result.after) +
		                             ",\"staging_address\":\"0x" + staging + "\",\"stack_restored\":" +
		                             (result.stack_restored ? "true" : "false") +
		                             ",\"elapsed_cycles\":" + std::to_string(result.elapsed_cycles) + "}";
		return lucent::http::Response::json(200, "OK", response);
	}
} // namespace AVPE::NativeCameraRoute
