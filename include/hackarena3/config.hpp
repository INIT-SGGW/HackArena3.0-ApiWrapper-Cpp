#pragma once

#include <optional>
#include <string>

namespace hackarena3 {

inline constexpr char kEnvApiUrl[] = "HA3_WRAPPER_API_URL";
inline constexpr char kEnvHaAuthBin[] = "HA3_WRAPPER_HA_AUTH_BIN";
inline constexpr char kEnvBackendEndpoint[] = "HA3_WRAPPER_BACKEND_ENDPOINT";
inline constexpr char kEnvTeamToken[] = "HA3_WRAPPER_TEAM_TOKEN";
inline constexpr char kEnvAuthToken[] = "HA3_WRAPPER_AUTH_TOKEN";

struct RuntimeConfig {
    std::string api_addr;
    std::optional<std::string> ha_auth_bin;
    std::optional<std::string> sandbox_id;
};

struct OfficialRuntimeConfig {
    std::string grpc_target;
    std::string rpc_prefix;
    std::string team_token;
    std::string auth_token;
};

RuntimeConfig load_runtime_config(bool require_api_addr = true);
OfficialRuntimeConfig load_official_runtime_config();

}  // namespace hackarena3
