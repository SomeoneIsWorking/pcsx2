// Small JSON field extractors for AVPE's bounded control routes.

#include "AVPE/HttpJson.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace AVPE::HttpJson
{
	std::optional<std::string> StringField(const std::string& body, const std::string& key)
	{
		const std::string needle = "\"" + key + "\"";
		size_t position = body.find(needle);
		if (position == std::string::npos)
			return std::nullopt;
		position = body.find(':', position + needle.size());
		if (position == std::string::npos)
			return std::nullopt;
		position = body.find('"', position + 1);
		if (position == std::string::npos)
			return std::nullopt;
		const size_t end = body.find('"', position + 1);
		if (end == std::string::npos)
			return std::nullopt;
		return body.substr(position + 1, end - position - 1);
	}

	std::optional<float> FloatField(const std::string& body, const std::string& key)
	{
		const std::string needle = "\"" + key + "\"";
		size_t position = body.find(needle);
		if (position == std::string::npos)
			return std::nullopt;
		position = body.find(':', position + needle.size());
		if (position == std::string::npos)
			return std::nullopt;
		++position;
		while (position < body.size() && (body[position] == ' ' || body[position] == '\t'))
			++position;
		if (position >= body.size())
			return std::nullopt;

		const char* const start = body.c_str() + position;
		char* end = nullptr;
		errno = 0;
		const float value = std::strtof(start, &end);
		if (end == start || errno == ERANGE || !std::isfinite(value))
			return std::nullopt;
		while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
			++end;
		if (*end != ',' && *end != '}')
			return std::nullopt;
		return value;
	}

	std::optional<u32> HexU32Field(const std::string& body, const std::string& key)
	{
		const std::optional<std::string> text = StringField(body, key);
		if (!text || text->size() != 10 || text->compare(0, 2, "0x") != 0)
			return std::nullopt;

		u32 value = 0;
		for (size_t index = 2; index < text->size(); ++index)
		{
			const char digit = (*text)[index];
			if (digit >= '0' && digit <= '9')
				value = (value << 4) | static_cast<u32>(digit - '0');
			else if (digit >= 'a' && digit <= 'f')
				value = (value << 4) | static_cast<u32>(digit - 'a' + 10);
			else
				return std::nullopt;
		}
		return value;
	}
} // namespace AVPE::HttpJson
