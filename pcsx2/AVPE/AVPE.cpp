// AVPE control channel — see AVPE.h. Fork-local; not for upstream.
#include "AVPE/AVPE.h"
#include "Host.h"
#include "MTGS.h"
#include "VMManager.h"
#include "vtlb.h"

#include "common/Console.h"
#include "common/Error.h"

#include <lucent/http.h>
#include <lucent/log.h>
#include <lucent/config.h>

#include <cstring>
#include <chrono>
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
			const auto s = json_string_field(body, key);
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
		const auto hexs = json_string_field(req.body, "hex");
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
		body += "]}";;
		return lucent::http::Response::json(200, "OK", body);
	}

	// GET /debug — host-side ground truth: transfer counter + last pad FIFO.
	static lucent::http::Response handle_debug()
	{
		char buf[512];
		const std::string fifo = LastFifo();
		std::snprintf(buf, sizeof(buf),
			R"({"transfers":%u,"lastfifo":"%s","inject":"%04x"})",
			TransferCount(), fifo.c_str(), ActiveButtonMask());
		return lucent::http::Response::json(200, "OK", buf);
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
			bmp[o] = v & 0xff; bmp[o+1] = (v>>8)&0xff; bmp[o+2] = (v>>16)&0xff; bmp[o+3] = (v>>24)&0xff;
		};
		put16(0, 0x4D42);              // 'BM'
		put32(2, file_size);
		put32(10, 54);
		put32(14, 40);                 // BITMAPINFOHEADER
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
				bmp[o++] = static_cast<u8>(px & 0xff);         // B
				bmp[o++] = static_cast<u8>((px >> 8) & 0xff);   // G
				bmp[o++] = static_cast<u8>((px >> 16) & 0xff);  // R
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
		if (req.method == "GET" && path == "/snap")
			return handle_snap();
		if (req.method == "POST" && path == "/mem/write")
			return handle_mem_write(req.body);
		if (req.method == "POST" && path == "/state/save")
			return handle_state_save(req.body);
		if (req.method == "POST" && path == "/state/load")
			return handle_state_load(req.body);
		if (req.method == "POST" && path == "/input/press")
			return handle_input_press(req.body);

		// Negative must be loud: name what was requested and what exists.
		lucent::warn("avpe", "no route: {} {}", req.method, path);
		return lucent::http::Response::json(404, "Not Found",
			"{\"routes\":[\"GET /status\",\"GET /mem/read\",\"GET /mem/scan\",\"GET /debug\","
			"\"GET /snap\",\"POST /mem/write\",\"POST /state/save\",\"POST /state/load\","
			"\"POST /input/press\"]}");
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
			4,  // PAD_TRIANGLE
			5,  // PAD_CIRCLE
			6,  // PAD_CROSS
			7,  // PAD_SQUARE
			8,  // PAD_SELECT
			11, // PAD_START
			2,  // PAD_L1
			0,  // PAD_L2
			3,  // PAD_R1
			1,  // PAD_R2
			9,  // PAD_L3
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
