#pragma once

#include <optional>
#include <string>

namespace hackarena3::detail {

std::optional<std::string> get_env(const char* key);
void set_env_if_unset(const std::string& key, const std::string& value);

}  // namespace hackarena3::detail
