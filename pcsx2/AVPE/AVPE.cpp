// AVPE control channel — see AVPE.h. Fork-local; not for upstream.
#include "AVPE/AVPE.h"
#include "Host.h"
#include "VMManager.h"
#include "vtlb.h"

#include "common/Console.h"
#include "common/Error.h"

#include <lucent/http.h>
#include <lucent/log.h>
#include <lucent/config.h>

#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace AVPE {
	static std::optional<lucent::http::Server> s_server;
	static std::once_flag s_start_once;

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
														  std::string_view::npos : amp - pos);
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
			if (c >= '0' && c <= '9') d = c - '0';
			else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
			else return false; // any non-hex character refuses the whole value
			v = (v << 4) | static_cast<u32>(d);
		}
		*out = v;
		return true;
	}

	static int hex_val(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
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

	// Minimal JSON string-field extractor: finds "key":"value" (no escapes).
	static std::optional<std::string> json_string_field(const std::string& body, const std::string& key)
	{
		const std::string needle = "\"" + key + "\"";
		size_t p = body.find(needle);
		if (p == std::string::npos)
			return std::nullopt;
		p = body.find(':', p + needle.size());
		if (p == std::string::npos)
			return std::nullopt;
		p = body.find('"', p + 1);
		if (p == std::string::npos)
			return std::nullopt;
		const size_t e = body.find('"', p + 1);
		if (e == std::string::npos)
			return std::nullopt;
		return body.substr(p + 1, e - p - 1);
	}

	// ------------------------------------------------------------- handlers

	static lucent::http::Response handle_status()
	{
		const VMState st = VMManager::GetState();
		std::string state = "Shutdown";
		switch (st)
		{
			case VMState::Initializing: state = "Initializing"; break;
			case VMState::Running:      state = "Running"; break;
			case VMState::Paused:       state = "Paused"; break;
			case VMState::Resetting:    state = "Resetting"; break;
			case VMState::Stopping:     state = "Stopping"; break;
			default: break;
		}
		char buf[256];
		std::snprintf(buf, sizeof(buf),
			R"({"vm":"%s","serial":"%s","crc":"%08X"})",
			state.c_str(), VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC());
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
		const auto addrs = json_string_field(body, "addr");
		const auto hexs = json_string_field(body, "hex");
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
		const auto path = json_string_field(body, "path");
		if (!path || path->empty())
			return lucent::http::Response::text(400, "Bad Request", "need path\n");
		std::string error;
		Host::RunOnCPUThread([&]() {
			VMManager::SaveState(path->c_str(), false, false,
				[&error](const std::string& err) { error = err; });
			VMManager::WaitForSaveStateFlush();
		}, true);
		const bool ok = error.empty();
		lucent::info("avpe", "savestate -> {} ({})", *path, ok ? "ok" : error);
		return lucent::http::Response::json(ok ? 200 : 500, ok ? "OK" : "Error",
			ok ? "{\"saved\":true}" : "{\"saved\":false}");
	}

	// POST /state/load {"path":"..."} — runs on the CPU thread, blocking.
	static lucent::http::Response handle_state_load(const std::string& body)
	{
		const auto path = json_string_field(body, "path");
		if (!path || path->empty())
			return lucent::http::Response::text(400, "Bad Request", "need path\n");
		Error error;
		bool ok = false;
		Host::RunOnCPUThread([&]() { ok = VMManager::LoadState(path->c_str(), &error); }, true);
		lucent::info("avpe", "loadstate {} ({})", *path, ok ? "ok" : error.GetDescription());
		return lucent::http::Response::json(ok ? 200 : 500, ok ? "OK" : "Error",
			ok ? "{\"loaded\":true}" : "{\"loaded\":false}");
	}

	static lucent::http::Response dispatch(const lucent::http::Request& req)
	{
		const std::string path(req.path());
		if (req.method == "GET" && path == "/status")
			return handle_status();
		if (req.method == "GET" && path == "/mem/read")
			return handle_mem_read(req);
		if (req.method == "POST" && path == "/mem/write")
			return handle_mem_write(req.body);
		if (req.method == "POST" && path == "/state/save")
			return handle_state_save(req.body);
		if (req.method == "POST" && path == "/state/load")
			return handle_state_load(req.body);

		// Negative must be loud: name what was requested and what exists.
		lucent::warn("avpe", "no route: {} {}", req.method, path);
		return lucent::http::Response::json(404, "Not Found",
			"{\"routes\":[\"GET /status\",\"GET /mem/read\",\"POST /mem/write\","
			"\"POST /state/save\",\"POST /state/load\"]}");
	}

	bool Start()
	{
		std::call_once(s_start_once, []() {
			lucent::config::set_prefix("AVPE_");
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
} // namespace AVPE
