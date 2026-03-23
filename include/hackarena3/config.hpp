#pragma once

#include <optional>
#include <string>

namespace hackarena3 {

inline constexpr char kEnvApiUrl[] = "HA3_WRAPPER_API_URL";
inline constexpr char kEnvHaAuthBin[] = "HA3_WRAPPER_HA_AUTH_BIN";

struct RuntimeConfig {
    std::string api_addr;
    std::optional<std::string> ha_auth_bin;
    std::optional<std::string> sandbox_id;
    std::string api_addr_source {"explicit"};
    std::optional<std::string> dotenv_path;
    bool dotenv_loaded {false};
};

RuntimeConfig load_runtime_config();

}  // namespace hackarena3
