#include "hackarena3/config.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "detail/environment.hpp"
#include "detail/string_utils.hpp"
#include "hackarena3/errors.hpp"

namespace {

struct DotenvLoadResult {
    std::filesystem::path path;
    bool loaded {};
};

DotenvLoadResult load_dotenv_if_present() {
    const auto dotenv_path = std::filesystem::current_path() / "user" / ".env";
    if (!std::filesystem::is_regular_file(dotenv_path)) {
        return DotenvLoadResult {.path = dotenv_path, .loaded = false};
    }

    std::ifstream input(dotenv_path);
    std::string raw_line;
    while (std::getline(input, raw_line)) {
        const auto line = hackarena3::detail::trim(raw_line);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        const auto key = hackarena3::detail::trim(line.substr(0, pos));
        if (key.empty()) {
            continue;
        }

        const auto value = hackarena3::detail::strip_matching_quotes(
            hackarena3::detail::trim(line.substr(pos + 1))
        );
        hackarena3::detail::set_env_if_unset(key, value);
    }

    return DotenvLoadResult {.path = dotenv_path, .loaded = true};
}

std::pair<std::string, std::string> required_api_addr(bool api_env_was_preexisting, bool dotenv_loaded) {
    const auto api_url = hackarena3::detail::get_env(hackarena3::kEnvApiUrl);
    const auto trimmed = hackarena3::detail::trim(api_url.value_or(""));
    if (!trimmed.empty()) {
        if (api_env_was_preexisting) {
            return {trimmed, "environment"};
        }
        if (dotenv_loaded) {
            return {trimmed, "user/.env"};
        }
        return {trimmed, "environment"};
    }
    throw hackarena3::ConfigError(
        std::string("Missing required runtime env: ") + hackarena3::kEnvApiUrl
    );
}

}  // namespace

namespace hackarena3 {

RuntimeConfig load_runtime_config() {
    const auto api_env_was_preexisting = detail::get_env(kEnvApiUrl).has_value();
    const auto dotenv_result = load_dotenv_if_present();
    const auto [api_addr, api_addr_source] = required_api_addr(
        api_env_was_preexisting,
        dotenv_result.loaded
    );

    RuntimeConfig config;
    config.api_addr = api_addr;
    config.api_addr_source = api_addr_source;
    config.dotenv_path = dotenv_result.path.string();
    config.dotenv_loaded = dotenv_result.loaded;

    const auto ha_auth_bin = detail::trim(detail::get_env(kEnvHaAuthBin).value_or(""));
    if (!ha_auth_bin.empty()) {
        config.ha_auth_bin = ha_auth_bin;
    }

    return config;
}

}  // namespace hackarena3
