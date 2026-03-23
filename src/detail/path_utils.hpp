#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace hackarena3::detail {

std::filesystem::path expand_user_path(const std::string& value);
std::optional<std::filesystem::path> resolve_executable_candidate(const std::string& candidate);
bool stdin_is_tty();

}  // namespace hackarena3::detail
