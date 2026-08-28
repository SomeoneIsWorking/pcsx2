// Small JSON field extractors for AVPE's bounded control routes.

#pragma once

#include <optional>
#include <string>

namespace AVPE::HttpJson
{
	std::optional<std::string> StringField(const std::string& body, const std::string& key);
	std::optional<float> FloatField(const std::string& body, const std::string& key);
} // namespace AVPE::HttpJson
