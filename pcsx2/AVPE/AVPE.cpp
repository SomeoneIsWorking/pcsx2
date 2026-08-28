// AVPE control channel — see AVPE.h. Fork-local; not for upstream.
#include "AVPE/AVPE.h"
#include "AVPE/EECallShuttle.h"
#include "AVPE/NativeAssets.h"
#include "AVPE/NativeAssetStateSnapshot.h"
#include "AVPE/NativeAssetByteTrace.h"
#include "AVPE/NativeBiosTrace.h"
#include "AVPE/NativeCdvdCompletion.h"
#include "AVPE/NativeInput.h"
#include "AVPE/NativeCameraRoute.h"
#include "AVPE/HttpJson.h"
#include "AVPE/NativeGuestReset.h"
#include "AVPE/NativeLoadTiming.h"
#include "AVPE/NativeMissionLoadTiming.h"
#include "AVPE/NativeMenuInput.h"
#include "Config.h"
#include "Host.h"
#include "Host/AudioStreamTypes.h"
#include "MTGS.h"
#include "R5900.h"
#include "VMManager.h"
#include "vtlb.h"

#include "common/Console.h"
#include "common/Error.h"

#include <lucent/http.h>
#include <lucent/log.h>
#include <lucent/config.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace AVPE
{
	static std::optional<lucent::http::Server> s_server;
	static std::once_flag s_start_once;
	static std::atomic_bool s_control_test_mode{false};
	static std::atomic_bool s_control_test_surface_verified{false};
	static std::string s_control_nonce;

	void SetSurfacelessControlTest(bool enabled)
	{
		s_control_test_mode.store(enabled, std::memory_order_release);
		s_control_test_surface_verified.store(false, std::memory_order_release);
		NativeAssets::ResetObservation();
		NativeAssetByteTrace::Reset();
		NativeLoadTiming::Reset();
		NativeMissionLoadTiming::Reset();
		NativeBiosTrace::SetEnabled(enabled);
	}

	bool IsSurfacelessControlTest()
	{
		return s_control_test_mode.load(std::memory_order_acquire);
	}

	void NoteControlTestRenderWindow(bool surfaceless)
	{
		if (IsSurfacelessControlTest())
			s_control_test_surface_verified.store(surfaceless, std::memory_order_release);
	}

	// ---------------------------------------------------------------- utils

	// Parses "k1=v1&k2=v2" (values are plain tokens; our callers use hex only).
	static std::string query_param(const lucent::http::Request& req, std::string_view key)
	{
		std::string_view q = req.query();
		size_t pos = 0;
		while (pos < q.size())
		{
			const size_t amp = q.find('&', pos);
			const std::string_view pair = q.substr(pos, (amp == std::string_view::npos) ?
															std::string_view::npos :
															amp - pos);
			const size_t eq = pair.find('=');
			if (eq != std::string_view::npos && pair.substr(0, eq) == key)
				return std::string(pair.substr(eq + 1));
			if (amp == std::string_view::npos)
				break;
			pos = amp + 1;
		}
		return {};
	}

	static bool parse_u32_hex(const std::string& s, u32* out)
	{
		size_t i = (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) ? 2 : 0;
		if (i >= s.size() || s.size() - i > 8)
			return false;
		u32 v = 0;
		for (; i < s.size(); ++i)
		{
			const char c = s[i];
			int d;
			if (c >= '0' && c <= '9')
				d = c - '0';
			else if (c >= 'a' && c <= 'f')
				d = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				d = c - 'A' + 10;
			else
				return false; // any non-hex character refuses the whole value
			v = (v << 4) | static_cast<u32>(d);
		}
		*out = v;
		return true;
	}

	static int hex_val(char c)
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	}

	static bool hex_to_bytes(const std::string& hex, std::vector<u8>* out)
	{
		if (hex.size() % 2 != 0)
			return false;
		out->clear();
		out->reserve(hex.size() / 2);
		for (size_t i = 0; i < hex.size(); i += 2)
		{
			const int hi = hex_val(hex[i]), lo = hex_val(hex[i + 1]);
			if (hi < 0 || lo < 0)
				return false;
			out->push_back(static_cast<u8>((hi << 4) | lo));
		}
		return true;
	}

	static std::string bytes_to_hex(const u8* data, size_t len)
	{
		static const char* d = "0123456789abcdef";
		std::string s;
		s.reserve(len * 2);
		for (size_t i = 0; i < len; ++i)
		{
			s.push_back(d[data[i] >> 4]);
			s.push_back(d[data[i] & 0xf]);
		}
		return s;
	}

	static std::string json_escape(const std::string_view value)
	{
		static constexpr char hex[] = "0123456789abcdef";
		std::string escaped;
		escaped.reserve(value.size());
		for (const char character : value)
		{
			switch (character)
			{
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\b':
					escaped += "\\b";
					break;
				case '\f':
					escaped += "\\f";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					if (static_cast<unsigned char>(character) < 0x20)
					{
						escaped += "\\u00";
						escaped.push_back(hex[(character >> 4) & 0xf]);
						escaped.push_back(hex[character & 0xf]);
					}
					else
					{
						escaped.push_back(character);
					}
					break;
			}
		}
		return escaped;
	}

	// ------------------------------------------------------------- handlers

	static lucent::http::Response handle_status()
	{
		const VMState st = VMManager::GetState();
		std::string state = "Shutdown";
		switch (st)
		{
			case VMState::Initializing:
				state = "Initializing";
				break;
			case VMState::Running:
				state = "Running";
				break;
			case VMState::Paused:
				state = "Paused";
				break;
			case VMState::Resetting:
				state = "Resetting";
				break;
			case VMState::Stopping:
				state = "Stopping";
				break;
			default:
				break;
		}
		const bool control_test = IsSurfacelessControlTest();
		const bool surfaceless = s_control_test_surface_verified.load(std::memory_order_acquire);
		const bool null_audio = EmuConfig.SPU2.Backend == AudioBackend::Null && EmuConfig.SPU2.OutputMuted;
		char buf[512];
		std::snprintf(buf, sizeof(buf),
			R"({"vm":"%s","serial":"%s","crc":"%08X","host_mode":"%s","surface":"%s","audio":"%s","nonce":"%s"})",
			state.c_str(), VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(),
			control_test ? "control-test" : "pcsx2",
			surfaceless ? "surfaceless" : "unverified",
			null_audio ? "null-muted" : "other", s_control_nonce.c_str());
		return lucent::http::Response::json(200, "OK", buf);
	}

	// GET /mem/read?addr=0x00195270&len=64
	static lucent::http::Response handle_mem_read(const lucent::http::Request& req)
	{
		u32 addr = 0;
		u32 len = 0;
		if (!parse_u32_hex(query_param(req, "addr"), &addr))
			return lucent::http::Response::text(400, "Bad Request", "bad addr\n");
		const std::string lens = query_param(req, "len");
		if (!parse_u32_hex(lens.empty() ? "" : lens, &len) || len == 0 || len > 4096)
			return lucent::http::Response::text(400, "Bad Request", "bad len (1..4096)\n");

		std::vector<u8> buf(len, 0);
		for (u32 i = 0; i < len; ++i)
			buf[i] = vtlb_ramRead<mem8_t>(addr + i);
		char head[128];
		std::snprintf(head, sizeof(head), R"({"addr":"0x%08X","len":%u,"hex":")", addr, len);
		std::string body = head;
		body += bytes_to_hex(buf.data(), buf.size());
		body += "\"}";
		return lucent::http::Response::json(200, "OK", body);
	}

	// POST /mem/write {"addr":"0x...","hex":"aabbcc"}
	static lucent::http::Response handle_mem_write(const std::string& body)
	{
		const auto addrs = HttpJson::StringField(body, "addr");
		const auto hexs = HttpJson::StringField(body, "hex");
		if (!addrs || !hexs)
			return lucent::http::Response::text(400, "Bad Request", "need addr+hex\n");
		u32 addr = 0;
		if (!parse_u32_hex(*addrs, &addr))
			return lucent::http::Response::text(400, "Bad Request", "bad addr\n");
		std::vector<u8> bytes;
		if (!hex_to_bytes(*hexs, &bytes) || bytes.empty() || bytes.size() > 4096)
			return lucent::http::Response::text(400, "Bad Request", "bad hex (1..4096)\n");
		for (size_t i = 0; i < bytes.size(); ++i)
			vtlb_ramWrite<mem8_t>(addr + static_cast<u32>(i), bytes[i]);
		lucent::info("avpe", "wrote {} bytes at {:08x}", bytes.size(), addr);
		return lucent::http::Response::json(200, "OK", "{\"written\":" + std::to_string(bytes.size()) + "}");
	}

	// POST /state/save {"path":"..."} — runs on the CPU thread, blocking.
	static lucent::http::Response handle_state_save(const std::string& body)
	{
		const auto path = HttpJson::StringField(body, "path");
		if (!path || path->empty())
			return lucent::http::Response::text(400, "Bad Request", "need path\n");
		std::string error;
		std::string native_asset_state;
		Host::RunOnCPUThread([&]() {
			native_asset_state = NativeAssetStateSnapshot::CaptureJsonOnCPUThread();
			VMManager::SaveState(path->c_str(), false, false,
				[&error](const std::string& err) { error = err; });
			VMManager::WaitForSaveStateFlush();
		},
			true);
		const bool ok = error.empty();
		lucent::info("avpe", "savestate -> {} ({})", *path, ok ? "ok" : error);
		const std::string response = ok ?
		                                 R"({"saved":true,"native_asset_state":)" + native_asset_state + '}' :
		                                 R"({"saved":false})";
		return lucent::http::Response::json(ok ? 200 : 500, ok ? "OK" : "Error",
			response);
	}

	// POST /state/load {"path":"..."} — runs on the CPU thread, blocking.
	static lucent::http::Response handle_state_load(const std::string& body)
	{
		const auto path = HttpJson::StringField(body, "path");
		if (!path || path->empty())
			return lucent::http::Response::text(400, "Bad Request", "need path\n");
		Error error;
		bool ok = false;
		std::string native_asset_state;
		Host::RunOnCPUThread([&]() {
			ok = VMManager::LoadState(path->c_str(), &error);
			if (ok)
			{
				native_asset_state = NativeAssetStateSnapshot::CaptureJsonOnCPUThread();
				EECallShuttle::ResetAfterStateLoad();
				NativeInput::ResetAfterStateLoad();
			}
		},
			true);
		lucent::info("avpe", "loadstate {} ({})", *path, ok ? "ok" : error.GetDescription());
		const std::string response = ok ?
		                                 R"({"loaded":true,"native_asset_state":)" + native_asset_state + '}' :
		                                 R"({"loaded":false})";
		return lucent::http::Response::json(ok ? 200 : 500, ok ? "OK" : "Error",
			response);
	}

	static lucent::http::Response handle_shutdown()
	{
		Host::RequestVMShutdown(false, false, false);
		return lucent::http::Response::json(202, "Accepted", "{\"shutdown\":true}");
	}

	// Decimal unless 0x-prefixed. Bare digits MUST NOT be read as hex.
	static bool parse_u32_auto(const std::string& s, u32* out)
	{
		if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0)
			return parse_u32_hex(s, out);
		if (s.empty() || s.size() > 10)
			return false;
		u64 v = 0;
		for (const char c : s)
		{
			if (c < '0' || c > '9')
				return false;
			v = v * 10u + static_cast<u64>(c - '0');
			if (v > 0xffffffffull)
				return false;
		}
		*out = static_cast<u32>(v);
		return true;
	}

	// Minimal JSON u32 field: accepts "0x1A"/"1A"-style strings and bare decimals.
	static std::optional<u32> json_u32_field(const std::string& body, const std::string& key)
	{
		const std::string needle = "\"" + key + "\"";
		size_t p = body.find(needle);
		if (p == std::string::npos)
			return std::nullopt;
		p = body.find(':', p + needle.size());
		if (p == std::string::npos)
			return std::nullopt;
		++p;
		while (p < body.size() && (body[p] == ' ' || body[p] == '\t'))
			++p;
		if (p < body.size() && body[p] == '"')
		{
			const auto s = HttpJson::StringField(body, key);
			if (!s)
				return std::nullopt;
			u32 qv = 0;
			if (!parse_u32_auto(*s, &qv))
				return std::nullopt;
			return qv;
		}
		std::string tok;
		while (p < body.size() &&
			   (isdigit(static_cast<unsigned char>(body[p])) || body[p] == 'x' ||
				   (body[p] >= 'a' && body[p] <= 'f') || (body[p] >= 'A' && body[p] <= 'F')))
		{
			tok += body[p];
			++p;
		}
		if (tok.empty())
			return std::nullopt;
		u32 v = 0;
		if (!parse_u32_auto(tok, &v))
			return std::nullopt;
		return v;
	}

	// POST /input/press {"mask":512,"ms":250} — PadDualshock2::Inputs bit space.
	static lucent::http::Response handle_input_press(const std::string& body)
	{
		const auto mask = json_u32_field(body, "mask");
		if (!mask || *mask == 0)
			return lucent::http::Response::text(400, "Bad Request", "need mask\n");
		u32 ms = 250;
		if (const auto mss = json_u32_field(body, "ms"))
			ms = *mss;
		PressButtons(*mask, ms);
		return lucent::http::Response::json(200, "OK", "{\"pressed\":true}");
	}

	static lucent::http::Response handle_input_move_absolute(const std::string& body)
	{
		const auto x = HttpJson::FloatField(body, "x");
		const auto y = HttpJson::FloatField(body, "y");
		if (!x || !y)
			return lucent::http::Response::text(400, "Bad Request", "need finite numeric x+y\n");

		const NativeInput::Result result = NativeInput::MoveAbsolute(*x, *y);
		if (!result.Succeeded())
		{
			int status = result.shuttle_status == EECallShuttle::Status::Busy ? 409 : 500;
			switch (result.status)
			{
				case NativeInput::Status::InvalidCoordinates:
					status = 400;
					break;
				case NativeInput::Status::PointerUnavailable:
				case NativeInput::Status::SelectorModeRejected:
					status = 409;
					break;
				case NativeInput::Status::ResolutionUnavailable:
					status = 503;
					break;
				default:
					break;
			}
			lucent::error("avpe-input", "absolute move ({}, {}) failed: {}", *x, *y, result.error);
			return lucent::http::Response::text(status, "Native Input Failed", std::string(result.error) + "\n");
		}

		char response[384];
		std::snprintf(response, sizeof(response),
			R"({"pointer":"0x%08X","screen_x":%.6f,"screen_y":%.6f,"observed_x":%.6f,"observed_y":%.6f,"staging_address":"0x%08X","stack_restored":%s,"elapsed_cycles":%llu})",
			result.pointer, result.screen_x, result.screen_y, result.observed_x, result.observed_y,
			result.staging_address, result.stack_restored ? "true" : "false",
			static_cast<unsigned long long>(result.elapsed_cycles));
		return lucent::http::Response::json(200, "OK", response);
	}

	static lucent::http::Response handle_input_mouse_button(const std::string& body)
	{
		const auto button_name = HttpJson::StringField(body, "button");
		const auto edge_name = HttpJson::StringField(body, "edge");
		if (!button_name || !edge_name)
			return lucent::http::Response::text(400, "Bad Request", "need button+edge\n");

		NativeInput::MouseButton button;
		if (*button_name == "primary")
			button = NativeInput::MouseButton::Primary;
		else if (*button_name == "secondary")
			button = NativeInput::MouseButton::Secondary;
		else
			return lucent::http::Response::text(400, "Bad Request", "button must be primary or secondary\n");

		NativeInput::ButtonEdge edge;
		if (*edge_name == "press")
			edge = NativeInput::ButtonEdge::Press;
		else if (*edge_name == "release")
			edge = NativeInput::ButtonEdge::Release;
		else
			return lucent::http::Response::text(400, "Bad Request", "edge must be press or release\n");

		const NativeInput::ButtonResult result = NativeInput::ApplyButtonEdge(button, edge);
		if (!result.Succeeded())
		{
			int status = 500;
			switch (result.status)
			{
				case NativeInput::Status::InvalidButtonEdge:
				case NativeInput::Status::PointerUnavailable:
					status = 409;
					break;
				default:
					break;
			}
			lucent::error("avpe-input", "{} {} failed: {}", *button_name, *edge_name, result.error);
			return lucent::http::Response::text(
				status, "Native Input Failed", std::string(result.error) + "\n");
		}

		char response[768];
		std::snprintf(response, sizeof(response),
			R"({"button":"%s","edge":"%s","pointer":"0x%08X","handler":"0x%08X","before":{"count":%u,"selected_mark":"0x%08X","selected_object":"0x%08X","command_id":"0x%08X"},"after":{"count":%u,"selected_mark":"0x%08X","selected_object":"0x%08X","command_id":"0x%08X"},"elapsed_cycles":%llu})",
			button_name->c_str(), edge_name->c_str(), result.pointer, result.handler,
			result.before.count, result.before.selected_mark, result.before.selected_object,
			result.before.command_id, result.after.count, result.after.selected_mark,
			result.after.selected_object, result.after.command_id,
			static_cast<unsigned long long>(result.elapsed_cycles));
		return lucent::http::Response::json(200, "OK", response);
	}

	static lucent::http::Response handle_input_menu_action(const std::string& body)
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
			int status = result.shuttle_status == EECallShuttle::Status::Busy ? 409 : 500;
			switch (result.status)
			{
				case NativeMenuInput::Status::MenuUnavailable:
				case NativeMenuInput::Status::AmbiguousMenu:
					status = 409;
					break;
				default:
					break;
			}
			lucent::error("avpe-input", "menu {} failed: {}", *action_name, result.error);
			return lucent::http::Response::text(
				status, "Native Menu Input Failed", std::string(result.error) + "\n");
		}

		char response[704];
		std::snprintf(response, sizeof(response),
			R"({"action":"%s","menu":"0x%08X","handler":"0x%08X","callback_count":%u,"before":{"focus_handle":"0x%08X","focus_object":"0x%08X"},"after":{"focus_handle":"0x%08X","focus_object":"0x%08X"},"elapsed_cycles":%llu,"deferred":%s,"deferred_call_id":%llu})",
			action_name->c_str(), result.menu, result.handler, result.callback_count, result.before.handle,
			result.before.object, result.after.handle, result.after.object,
			static_cast<unsigned long long>(result.elapsed_cycles), result.deferred ? "true" : "false",
			static_cast<unsigned long long>(result.deferred_call_id));
		return lucent::http::Response::json(result.deferred ? 202 : 200,
			result.deferred ? "Accepted" : "OK", response);
	}

	static lucent::http::Response handle_input_menu_state()
	{
		const NativeMenuInput::Result result = NativeMenuInput::Inspect();
		if (!result.Succeeded())
		{
			const int status =
				(result.status == NativeMenuInput::Status::MenuUnavailable ||
					result.status == NativeMenuInput::Status::AmbiguousMenu) ?
					409 :
					500;
			return lucent::http::Response::text(
				status, "Native Menu State Unavailable", std::string(result.error) + "\n");
		}
		char response[320];
		std::snprintf(response, sizeof(response),
			R"({"menu":"0x%08X","callback_count":%u,"focus_handle":"0x%08X","focus_object":"0x%08X"})",
			result.menu, result.callback_count, result.before.handle, result.before.object);
		return lucent::http::Response::json(200, "OK", response);
	}

	static int menu_pointer_failure_status(const NativeMenuInput::PointerResult& result)
	{
		if (result.shuttle_status == EECallShuttle::Status::Busy)
			return 409;
		switch (result.status)
		{
			case NativeMenuInput::Status::InvalidCoordinates:
				return 400;
			case NativeMenuInput::Status::PointerUnavailable:
			case NativeMenuInput::Status::AmbiguousPointer:
			case NativeMenuInput::Status::FocusUnavailable:
				return 409;
			case NativeMenuInput::Status::ResolutionUnavailable:
				return 503;
			default:
				return 500;
		}
	}

	static lucent::http::Response menu_pointer_response(
		const NativeMenuInput::PointerResult& result)
	{
		char response[768];
		std::snprintf(response, sizeof(response),
			R"({"pointer":"0x%08X","handler":"0x%08X","callback_count":%u,"before":{"focus_handle":"0x%08X","focus_object":"0x%08X"},"after":{"focus_handle":"0x%08X","focus_object":"0x%08X"},"screen_x":%.6f,"screen_y":%.6f,"observed_x":%.6f,"observed_y":%.6f,"staging_address":"0x%08X","stack_restored":%s,"elapsed_cycles":%llu,"deferred":%s,"deferred_call_id":%llu})",
			result.pointer, result.handler, result.callback_count, result.before.handle,
			result.before.object, result.after.handle, result.after.object, result.screen_x,
			result.screen_y, result.observed_x, result.observed_y, result.staging_address,
			result.stack_restored ? "true" : "false",
			static_cast<unsigned long long>(result.elapsed_cycles), result.deferred ? "true" : "false",
			static_cast<unsigned long long>(result.deferred_call_id));
		return lucent::http::Response::json(result.deferred ? 202 : 200,
			result.deferred ? "Accepted" : "OK", response);
	}

	static lucent::http::Response handle_input_menu_pointer_state()
	{
		const NativeMenuInput::PointerResult result = NativeMenuInput::InspectPointer();
		if (!result.Succeeded())
		{
			return lucent::http::Response::text(menu_pointer_failure_status(result),
				"Native Menu Pointer Unavailable", std::string(result.error) + "\n");
		}
		return menu_pointer_response(result);
	}

	static lucent::http::Response handle_input_menu_pointer_move(const std::string& body)
	{
		const auto x = HttpJson::FloatField(body, "x");
		const auto y = HttpJson::FloatField(body, "y");
		if (!x || !y)
			return lucent::http::Response::text(400, "Bad Request", "need finite numeric x+y\n");

		const NativeMenuInput::PointerResult result = NativeMenuInput::MovePointer(*x, *y);
		if (!result.Succeeded())
		{
			lucent::error("avpe-input", "menu pointer move ({}, {}) failed: {}", *x, *y, result.error);
			return lucent::http::Response::text(menu_pointer_failure_status(result),
				"Native Menu Pointer Failed", std::string(result.error) + "\n");
		}
		return menu_pointer_response(result);
	}

	static lucent::http::Response handle_input_menu_pointer_activate()
	{
		const NativeMenuInput::PointerResult result = NativeMenuInput::ActivatePointer();
		if (!result.Succeeded())
		{
			lucent::error("avpe-input", "menu pointer activation failed: {}", result.error);
			return lucent::http::Response::text(menu_pointer_failure_status(result),
				"Native Menu Pointer Failed", std::string(result.error) + "\n");
		}
		return menu_pointer_response(result);
	}

	static const char* deferred_state_name(const EECallShuttle::DeferredState state)
	{
		switch (state)
		{
			case EECallShuttle::DeferredState::Idle:
				return "idle";
			case EECallShuttle::DeferredState::Running:
				return "running";
			case EECallShuttle::DeferredState::Completed:
				return "completed";
			case EECallShuttle::DeferredState::Failed:
				return "failed";
		}
		return "failed";
	}

	static lucent::http::Response handle_ee_deferred()
	{
		const EECallShuttle::DeferredSnapshot snapshot = EECallShuttle::GetDeferredSnapshot();
		char response[384];
		std::snprintf(response, sizeof(response),
			R"({"id":%llu,"state":"%s","succeeded":%s,"v0":"0x%016llX","v1":"0x%016llX","return_pc":"0x%08X","staging_address":"0x%08X","stack_restored":%s,"elapsed_cycles":%llu,"error":"%s"})",
			static_cast<unsigned long long>(snapshot.id), deferred_state_name(snapshot.state),
			snapshot.result.Succeeded() ? "true" : "false",
			static_cast<unsigned long long>(snapshot.result.v0),
			static_cast<unsigned long long>(snapshot.result.v1), snapshot.result.stopped_pc,
			snapshot.result.staging_address, snapshot.result.stack_restored ? "true" : "false",
			static_cast<unsigned long long>(snapshot.result.elapsed_cycles), snapshot.result.error);
		return lucent::http::Response::json(200, "OK", response);
	}

	// POST /ee/call {"function":"0x00137b30","a0":0,"cycle_budget":3000000}
	static lucent::http::Response handle_ee_call(const std::string& body)
	{
		const auto function = json_u32_field(body, "function");
		if (!function)
			return lucent::http::Response::text(400, "Bad Request", "need function\n");

		EECallShuttle::Request request{.function = *function};
		for (u32 i = 0; i < request.arguments.size(); ++i)
		{
			if (const auto argument = json_u32_field(body, "a" + std::to_string(i)))
				request.arguments[i] = *argument;
		}
		if (const auto cycle_budget = json_u32_field(body, "cycle_budget"))
			request.cycle_budget = *cycle_budget;

		const auto stack_argument = json_u32_field(body, "stack_argument");
		const auto stack_hex = HttpJson::StringField(body, "stack_hex");
		if (stack_argument.has_value() != stack_hex.has_value())
			return lucent::http::Response::text(400, "Bad Request", "stack_argument and stack_hex must be paired\n");
		std::vector<u8> stack_bytes;
		if (stack_hex && (!hex_to_bytes(*stack_hex, &stack_bytes) || stack_bytes.empty()))
			return lucent::http::Response::text(400, "Bad Request", "stack_hex must contain bytes\n");

		EECallShuttle::Result result;
		EECallShuttle::RunTransaction([&](EECallShuttle::Transaction& transaction) {
			result = stack_argument ?
			             transaction.CallWithStackBuffer(request, *stack_argument, stack_bytes) :
			             transaction.Call(request);
		});
		if (!result.Succeeded())
		{
			int status = 500;
			switch (result.status)
			{
				case EECallShuttle::Status::InvalidRequest:
					status = 400;
					break;
				case EECallShuttle::Status::WrongGame:
				case EECallShuttle::Status::Busy:
				case EECallShuttle::Status::Faulted:
					status = 409;
					break;
				case EECallShuttle::Status::VMUnavailable:
					status = 503;
					break;
				case EECallShuttle::Status::CycleBudgetExceeded:
					status = 504;
					break;
				default:
					break;
			}
			lucent::error("avpe", "EE call {:08x} failed: {}", request.function, result.error);
			return lucent::http::Response::text(status, "EE Call Failed", std::string(result.error) + "\n");
		}

		char response[384];
		std::snprintf(response, sizeof(response),
			R"({"v0":"0x%016llX","v1":"0x%016llX","stopped_pc":"0x%08X","staging_address":"0x%08X","stack_restored":%s,"elapsed_cycles":%llu})",
			static_cast<unsigned long long>(result.v0), static_cast<unsigned long long>(result.v1),
			result.stopped_pc, result.staging_address, result.stack_restored ? "true" : "false",
			static_cast<unsigned long long>(result.elapsed_cycles));
		lucent::info("avpe", "EE call {:08x} returned after {} cycles", request.function, result.elapsed_cycles);
		return lucent::http::Response::json(200, "OK", response);
	}

	// GET /mem/scan?start=0x..&end=0x..&hex=aabb — first match address or {"found":false}.
	// Bounded: range capped at 4 MiB, pattern at 256 bytes. Scans are exact-byte.
	static lucent::http::Response handle_mem_scan(const lucent::http::Request& req)
	{
		u32 start = 0, end = 0;
		if (!parse_u32_hex(query_param(req, "start"), &start))
			return lucent::http::Response::text(400, "Bad Request", "bad start\n");
		if (!parse_u32_hex(query_param(req, "end"), &end) || end <= start ||
			end - start > 0x400000)
			return lucent::http::Response::text(400, "Bad Request", "bad end (range<=4MiB)\n");
		const auto hexs = HttpJson::StringField(req.body, "hex");
		std::vector<u8> pat;
		const std::string qpat = query_param(req, "hex");
		if (!hex_to_bytes(!qpat.empty() ? qpat : (hexs ? *hexs : ""), &pat) ||
			pat.empty() || pat.size() > 256)
			return lucent::http::Response::text(400, "Bad Request", "bad hex pattern\n");

		u32 hits = 0;
		std::vector<u32> addrs;
		for (u32 a = start; a + static_cast<u32>(pat.size()) <= end; ++a)
		{
			bool ok = true;
			for (size_t i = 0; i < pat.size(); ++i)
			{
				if (vtlb_ramRead<mem8_t>(a + static_cast<u32>(i)) != pat[i])
				{
					ok = false;
					break;
				}
			}
			if (ok)
			{
				if (addrs.size() < 16)
					addrs.push_back(a);
				++hits;
			}
		}
		std::string body = "{\"hits\":" + std::to_string(hits) + ",\"addrs\":[";
		for (size_t i = 0; i < addrs.size(); ++i)
		{
			char buf[20];
			std::snprintf(buf, sizeof(buf), "%s\"0x%08X\"", i ? "," : "", addrs[i]);
			body += buf;
		}
		body += "]}";
		return lucent::http::Response::json(200, "OK", body);
	}

	// GET /debug — host-side ground truth: transfer counter + last pad FIFO.
	static lucent::http::Response handle_debug()
	{
		char buf[512];
		const std::string fifo = LastFifo();
		std::snprintf(buf, sizeof(buf),
			R"({"transfers":%u,"lastfifo":"%s","inject":"%04x","ee_pc":"0x%08X"})",
			TransferCount(), fifo.c_str(), ActiveButtonMask(), cpuRegs.pc);
		return lucent::http::Response::json(200, "OK", buf);
	}

	static lucent::http::Response handle_asset_opens()
	{
		const NativeAssets::ObservationSnapshot snapshot = NativeAssets::GetObservationSnapshot();
		const NativeCdvdCompletion::Snapshot completion = NativeCdvdCompletion::GetSnapshot();
		std::string body = "{\"enabled\":";
		body += snapshot.enabled ? "true" : "false";
		body += ",\"target_recognized\":";
		body += snapshot.target_recognized ? "true" : "false";
		body += ",\"total_open_calls\":" + std::to_string(snapshot.total_open_calls);
		body += ",\"dropped_unique_paths\":" + std::to_string(snapshot.dropped_unique_paths);
		body += ",\"cdvd_completion\":{\"recorded\":" + std::to_string(completion.recorded);
		body += ",\"consumed\":" + std::to_string(completion.consumed);
		body += ",\"consume_misses\":" + std::to_string(completion.consume_misses);
		body += ",\"rejected_records\":" + std::to_string(completion.rejected_records);
		body += ",\"active_tokens\":" + std::to_string(completion.active_tokens) + '}';
		body += ",\"paths\":[";
		for (size_t index = 0; index < snapshot.paths.size(); ++index)
		{
			const NativeAssets::OpenObservation& observation = snapshot.paths[index];
			if (index != 0)
				body += ',';
			body += "{\"path\":\"" + json_escape(observation.path) + "\",\"flags\":";
			body += std::to_string(observation.flags);
			body += ",\"count\":" + std::to_string(observation.count);
			body += ",\"native_open_count\":" + std::to_string(observation.native_open_count);
			body += ",\"original_fallback_count\":" + std::to_string(observation.original_fallback_count);
			body += ",\"refused_count\":" + std::to_string(observation.refused_count);
			body += ",\"read_calls\":" + std::to_string(observation.read_calls);
			body += ",\"bytes_read\":" + std::to_string(observation.bytes_read);
			body += ",\"seek_calls\":" + std::to_string(observation.seek_calls);
			body += ",\"close_count\":" + std::to_string(observation.close_count) + '}';
		}
		body += "]}";
		return lucent::http::Response::json(200, "OK", body);
	}

	static lucent::http::Response handle_asset_cache()
	{
		const NativeAssets::CacheSnapshot snapshot = NativeAssets::GetCacheSnapshot();
		std::string body = "{\"page_bytes\":" + std::to_string(NativeAssetCache::PageBytes);
		body += ",\"maximum_pages\":" + std::to_string(NativeAssetCache::MaximumPages);
		body += ",\"maximum_resident_bytes\":" + std::to_string(NativeAssetCache::MaximumResidentBytes);
		body += ",\"hits\":" + std::to_string(snapshot.hits);
		body += ",\"misses\":" + std::to_string(snapshot.misses);
		body += ",\"fills\":" + std::to_string(snapshot.fills);
		body += ",\"evictions\":" + std::to_string(snapshot.evictions);
		body += ",\"resident_pages\":" + std::to_string(snapshot.resident_pages);
		body += ",\"resident_bytes\":" + std::to_string(snapshot.resident_bytes);
		body += ",\"transient_handles\":" + std::to_string(snapshot.transient_handles);
		body += ",\"peak_transient_handles\":" + std::to_string(snapshot.peak_transient_handles) + '}';
		return lucent::http::Response::json(200, "OK", body);
	}

	static lucent::http::Response handle_bios_trace()
	{
		return lucent::http::Response::json(200, "OK", NativeBiosTrace::SnapshotJson());
	}

	static lucent::http::Response handle_bios_trace_capture()
	{
		std::string snapshot;
		Host::RunOnCPUThread(
			[&snapshot]() { snapshot = NativeBiosTrace::SnapshotAndDisableJson(); }, true);
		return lucent::http::Response::json(200, "OK", snapshot);
	}

	static lucent::http::Response handle_bios_trace_capture_at_guest_boundary()
	{
		const std::string snapshot = NativeBiosTrace::CaptureAtGuestBoundaryJson(std::chrono::seconds(5));
		if (snapshot.empty())
			return lucent::http::Response::text(504, "Gateway Timeout",
				"guest CPU frame boundary was not observed before the BIOS trace deadline\n");
		return lucent::http::Response::json(200, "OK", snapshot);
	}

	static lucent::http::Response handle_bios_trace_start()
	{
		NativeBiosTrace::SetEnabled(true);
		return lucent::http::Response::json(200, "OK", "{\"started\":true}");
	}

	static lucent::http::Response handle_asset_resolve(const std::string& body)
	{
		const std::optional<std::string> path = HttpJson::StringField(body, "path");
		const std::optional<std::string> access = HttpJson::StringField(body, "access");
		if (!path || !access || (*access != "read" && *access != "write"))
			return lucent::http::Response::text(400, "Bad Request", "need path and access=read|write\n");
		const bool read_only = *access == "read";
		const NativeAssets::OpenResolution resolution =
			NativeAssets::ResolveIomanOpen(*path, read_only ? 1 : 2, read_only);
		const char* disposition = "unhandled";
		switch (resolution.disposition)
		{
			case NativeAssets::OpenDisposition::NativeFile:
				disposition = "native-file";
				break;
			case NativeAssets::OpenDisposition::RefusedMissing:
				disposition = "refused-missing";
				break;
			case NativeAssets::OpenDisposition::RefusedAccess:
				disposition = "refused-access";
				break;
			case NativeAssets::OpenDisposition::RefusedInvalidStore:
				disposition = "refused-invalid-store";
				break;
			default:
				break;
		}
		return lucent::http::Response::json(200, "OK",
			"{\"disposition\":\"" + std::string(disposition) + "\"}");
	}

	static lucent::http::Response handle_asset_oracle_capture()
	{
		bool captured = false;
		Host::RunOnCPUThread([&captured]() { captured = NativeAssetByteTrace::CaptureIsoOracle(); }, true);
		return lucent::http::Response::json(captured ? 200 : 409,
			captured ? "OK" : "Conflict", captured ? "{\"captured\":true}" : "{\"captured\":false}");
	}

	// GET /snap — current frame as BMP (24-bit), so tooling can SEE the game.
	static lucent::http::Response handle_snap()
	{
		u32 width = 0, height = 0;
		std::vector<u32> pixels;
		if (!MTGS::SaveMemorySnapshot(0, 0, true, false, &width, &height, &pixels))
			return lucent::http::Response::text(503, "Unavailable", "no frame available\n");
		if (width == 0 || height == 0)
			return lucent::http::Response::text(500, "Error", "empty frame\n");

		// Minimal BMP writer: 24-bit bottom-up BGR, rows padded to 4 bytes.
		const u32 row_bytes = width * 3;
		const u32 row_pad = (4 - (row_bytes % 4)) % 4;
		const u32 data_size = (row_bytes + row_pad) * height;
		const u32 file_size = 54 + data_size;
		std::vector<u8> bmp(file_size);
		auto put16 = [&](size_t o, u16 v) { bmp[o] = v & 0xff; bmp[o+1] = v >> 8; };
		auto put32 = [&](size_t o, u32 v) {
			bmp[o] = v & 0xff;
			bmp[o + 1] = (v >> 8) & 0xff;
			bmp[o + 2] = (v >> 16) & 0xff;
			bmp[o + 3] = (v >> 24) & 0xff;
		};
		put16(0, 0x4D42); // 'BM'
		put32(2, file_size);
		put32(10, 54);
		put32(14, 40); // BITMAPINFOHEADER
		put32(18, width);
		put32(22, height);
		put16(26, 1);
		put16(28, 24);
		put32(34, data_size);
		size_t o = 54;
		for (u32 y = height; y-- > 0;)
		{
			const u32* row = &pixels[static_cast<size_t>(y) * width];
			for (u32 x = 0; x < width; ++x)
			{
				const u32 px = row[x];
				bmp[o++] = static_cast<u8>(px & 0xff); // B
				bmp[o++] = static_cast<u8>((px >> 8) & 0xff); // G
				bmp[o++] = static_cast<u8>((px >> 16) & 0xff); // R
			}
			o += row_pad;
		}
		lucent::info("avpe", "snap {}x{}", width, height);
		return lucent::http::Response::binary(200, "OK", "image/bmp",
			std::string(reinterpret_cast<const char*>(bmp.data()), bmp.size()));
	}

	static lucent::http::Response dispatch(const lucent::http::Request& req)
	{
		const std::string path(req.path());
		if (req.method == "GET" && path == "/status")
			return handle_status();
		if (req.method == "GET" && path == "/mem/read")
			return handle_mem_read(req);
		if (req.method == "GET" && path == "/mem/scan")
			return handle_mem_scan(req);
		if (req.method == "GET" && path == "/debug")
			return handle_debug();
		if (req.method == "GET" && path == "/assets/opens")
			return handle_asset_opens();
		if (req.method == "GET" && path == "/assets/cache")
			return handle_asset_cache();
		if (req.method == "GET" && path == "/assets/byte-trace")
			return lucent::http::Response::json(200, "OK", NativeAssetByteTrace::SnapshotJson());
		if (req.method == "GET" && path == "/assets/load-timing")
			return lucent::http::Response::json(200, "OK", NativeLoadTiming::SnapshotJson());
		if (req.method == "GET" && path == "/assets/mission-load-timing")
			return lucent::http::Response::json(200, "OK", NativeMissionLoadTiming::SnapshotJson());
		if (req.method == "GET" && path == "/bios/trace")
			return handle_bios_trace();
		if (req.method == "POST" && path == "/bios/trace/start")
			return handle_bios_trace_start();
		if (req.method == "POST" && path == "/bios/trace/capture")
			return handle_bios_trace_capture();
		if (req.method == "POST" && path == "/bios/trace/capture-at-guest-boundary")
			return handle_bios_trace_capture_at_guest_boundary();
		if (req.method == "GET" && path == "/ee/deferred")
			return handle_ee_deferred();
		if (req.method == "GET" && path == "/input/menu")
			return handle_input_menu_state();
		if (req.method == "GET" && path == "/input/menu-pointer")
			return handle_input_menu_pointer_state();
		if (req.method == "GET" && path == "/snap")
			return handle_snap();
		if (req.method == "POST" && path == "/mem/write")
			return handle_mem_write(req.body);
		if (req.method == "POST" && path == "/assets/resolve")
			return handle_asset_resolve(req.body);
		if (req.method == "POST" && path == "/assets/capture-iso-oracle")
			return handle_asset_oracle_capture();
		if (req.method == "POST" && path == "/guest/reset")
			return NativeGuestReset::Handle();
		if (req.method == "POST" && path == "/state/save")
			return handle_state_save(req.body);
		if (req.method == "POST" && path == "/state/load")
			return handle_state_load(req.body);
		if (req.method == "POST" && path == "/input/press")
			return handle_input_press(req.body);
		if (req.method == "POST" && path == "/input/move-absolute")
			return handle_input_move_absolute(req.body);
		if (req.method == "POST" && path == "/input/mouse-button")
			return handle_input_mouse_button(req.body);
		if (req.method == "POST" && path == "/input/camera")
			return NativeCameraRoute::Handle(req.body);
		if (req.method == "POST" && path == "/input/menu-action")
			return handle_input_menu_action(req.body);
		if (req.method == "POST" && path == "/input/menu-pointer-move")
			return handle_input_menu_pointer_move(req.body);
		if (req.method == "POST" && path == "/input/menu-pointer-activate")
			return handle_input_menu_pointer_activate();
		if (req.method == "POST" && path == "/ee/call")
			return handle_ee_call(req.body);
		if (req.method == "POST" && path == "/shutdown")
			return handle_shutdown();

		// Negative must be loud: name what was requested and what exists.
		lucent::warn("avpe", "no route: {} {}", req.method, path);
		return lucent::http::Response::json(404, "Not Found",
			"{\"routes\":[\"GET /status\",\"GET /mem/read\",\"GET /mem/scan\",\"GET /debug\","
			"\"GET /assets/opens\",\"GET /assets/cache\",\"GET /assets/byte-trace\",\"GET /assets/load-timing\",\"GET /bios/trace\",\"POST /bios/trace/start\",\"POST /bios/trace/capture\",\"POST /bios/trace/capture-at-guest-boundary\","
			"\"GET /ee/deferred\","
			"\"GET /input/menu\",\"GET /input/menu-pointer\","
			"\"GET /snap\",\"POST /mem/write\",\"POST /assets/resolve\","
			"\"POST /assets/capture-iso-oracle\","
			"\"POST /guest/reset\","
			"\"POST /state/save\",\"POST /state/load\","
			"\"POST /input/press\",\"POST /input/move-absolute\",\"POST /input/mouse-button\",\"POST /input/camera\","
			"\"POST /input/menu-action\",\"POST /input/menu-pointer-move\","
			"\"POST /input/menu-pointer-activate\","
			"\"POST /ee/call\",\"POST /shutdown\"]}");
	}

	bool Start()
	{
		std::call_once(s_start_once, []() {
			lucent::config::set_prefix("AVPE_");
			const char* const nonce = std::getenv("AVPE_CONTROL_NONCE");
			s_control_nonce = nonce ? nonce : "";
			const int port = lucent::config::number("HTTP_PORT", 28447);
			s_server.emplace(lucent::http::ServerOptions{.port = static_cast<u16>(port)}, &dispatch);
			if (!s_server->start())
			{
				lucent::error("avpe", "control server failed to bind 127.0.0.1:{}", port);
				s_server.reset();
				return;
			}
			lucent::info("avpe", "control channel on 127.0.0.1:{} (AVPE_HTTP_PORT to override)", port);
		});
		return s_server.has_value();
	}

	void Shutdown()
	{
		// lucent server stops with destruction; keep it alive until process exit.
	}

	// ------------------------------------------------------- button injection

	// (deadline_ms << 32) | mask; port 0 only — AVP:E is a single-controller game.
	static std::atomic<u64> s_button_inject{0};

	static u64 now_ms()
	{
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	void PressButtons(u32 inputs_mask, u32 ms)
	{
		// Translate PadDualshock2::Inputs bit space -> report ("buttons") bit
		// space. Table mirrors PadDualshock2.h bitmaskMapping — keep in sync.
		static constexpr u8 inputsToWire[16] = {
			12, // PAD_UP
			13, // PAD_RIGHT
			14, // PAD_DOWN
			15, // PAD_LEFT
			4, // PAD_TRIANGLE
			5, // PAD_CIRCLE
			6, // PAD_CROSS
			7, // PAD_SQUARE
			8, // PAD_SELECT
			11, // PAD_START
			2, // PAD_L1
			0, // PAD_L2
			3, // PAD_R1
			1, // PAD_R2
			9, // PAD_L3
			10, // PAD_R3
		};
		u32 wire = 0;
		for (u32 i = 0; i < 16; ++i)
		{
			if (inputs_mask & (1u << i))
				wire |= (1u << inputsToWire[i]);
		}
		const u64 deadline = now_ms() + ms;
		s_button_inject.store((deadline << 32) | wire, std::memory_order_release);
		lucent::info("avpe", "press inputs={:04x} wire={:04x} for {}ms", inputs_mask, wire, ms);
	}

	u32 ActiveButtonMask()
	{
		const u64 v = s_button_inject.load(std::memory_order_acquire);
		const u64 deadline = v >> 32;
		if (now_ms() >= deadline)
			return 0;
		return static_cast<u32>(v & 0xffffffffu);
	}

	static std::atomic<u32> s_transfers{0};
	static std::mutex s_fifo_mutex;
	static std::string s_last_fifo;

	void NotePadTransfer(int port, const char* fifo_bytes, u32 fifo_len)
	{
		s_transfers.fetch_add(1, std::memory_order_relaxed);
		if (port != 0 || fifo_len == 0)
			return;
		std::string hex;
		hex.reserve(fifo_len * 2);
		static const char* d = "0123456789abcdef";
		for (u32 i = 0; i < fifo_len; ++i)
		{
			hex.push_back(d[(fifo_bytes[i] >> 4) & 0xf]);
			hex.push_back(d[fifo_bytes[i] & 0xf]);
		}
		std::lock_guard lock(s_fifo_mutex);
		s_last_fifo = std::move(hex);
	}

	u32 TransferCount()
	{
		return s_transfers.load(std::memory_order_relaxed);
	}

	std::string LastFifo()
	{
		std::lock_guard lock(s_fifo_mutex);
		return s_last_fifo;
	}
} // namespace AVPE
