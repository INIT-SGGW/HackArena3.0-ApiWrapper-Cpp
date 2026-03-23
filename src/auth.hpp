#pragma once

#include <optional>
#include <string>

namespace hackarena3::detail {

std::string resolve_ha_auth_binary(const std::optional<std::string>& ha_auth_bin);
std::string fetch_member_jwt(const std::optional<std::string>& ha_auth_bin);

}  // namespace hackarena3::detail
