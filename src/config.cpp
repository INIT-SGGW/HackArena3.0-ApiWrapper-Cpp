#include "hackarena3/config.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "detail/environment.hpp"
#include "detail/string_utils.hpp"
#include "hackarena3/errors.hpp"

namespace {

struct ParsedOfficialEndpoint {
    std::string grpc_target;
    std::string rpc_prefix;
};

void load_dotenv_if_present() {
    const auto dotenv_path = std::filesystem::current_path() / "user" / ".env";
    if (!std::filesystem::is_regular_file(dotenv_path)) {
        return;
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

}

std::string required_api_addr() {
    const auto api_url = hackarena3::detail::get_env(hackarena3::kEnvApiUrl);
    const auto trimmed = hackarena3::detail::trim(api_url.value_or(""));
    if (!trimmed.empty()) {
        return trimmed;
    }
    throw hackarena3::ConfigError(
        std::string("Missing required runtime env: ") + hackarena3::kEnvApiUrl
    );
}

std::string optional_api_addr() {
    return hackarena3::detail::trim(
        hackarena3::detail::get_env(hackarena3::kEnvApiUrl).value_or("")
    );
}

int parse_port_or_default(std::string_view value, int default_port) {
    if (value.empty()) {
        return default_port;
    }

    try {
        const int port = std::stoi(std::string(value));
        if (port <= 0 || port > 65535) {
            throw std::out_of_range("port");
        }
        return port;
    } catch (const std::exception&) {
        throw hackarena3::ConfigError(
            "Invalid HA3_WRAPPER_BACKEND_ENDPOINT: invalid port in URL."
        );
    }
}

std::string format_target(std::string_view host, int port) {
    if (host.find(':') != std::string_view::npos && !host.starts_with('[')) {
        return "[" + std::string(host) + "]:" + std::to_string(port);
    }
    return std::string(host) + ":" + std::to_string(port);
}

ParsedOfficialEndpoint parse_official_endpoint(const std::string& endpoint) {
    constexpr std::string_view prefix = "https://";
    if (!endpoint.starts_with(prefix)) {
        throw hackarena3::ConfigError(
            "Invalid HA3_WRAPPER_BACKEND_ENDPOINT: expected https:// URL."
        );
    }

    const auto remainder = std::string_view(endpoint).substr(prefix.size());
    if (remainder.empty()) {
        throw hackarena3::ConfigError(
            "Invalid HA3_WRAPPER_BACKEND_ENDPOINT: missing host in URL."
        );
    }
    if (remainder.find_first_of("?#;") != std::string_view::npos) {
        throw hackarena3::ConfigError(
            "Invalid HA3_WRAPPER_BACKEND_ENDPOINT: query/fragment/params are not supported."
        );
    }

    const auto slash = remainder.find('/');
    const auto authority = slash == std::string_view::npos ? remainder : remainder.substr(0, slash);
    auto path = slash == std::string_view::npos ? std::string_view {} : remainder.substr(slash);
    if (authority.empty()) {
        throw hackarena3::ConfigError(
            "Invalid HA3_WRAPPER_BACKEND_ENDPOINT: missing host in URL."
        );
    }
    if (path.empty() || path == "/") {
        throw hackarena3::ConfigError(
            "Invalid HA3_WRAPPER_BACKEND_ENDPOINT: non-root path prefix is required (for example /backend)."
        );
    }

    std::string host;
    int port = 443;
    if (authority.starts_with('[')) {
        const auto closing = authority.find(']');
        if (closing == std::string_view::npos) {
            throw hackarena3::ConfigError("Invalid HA3_WRAPPER_BACKEND_ENDPOINT URL.");
        }
        host = std::string(authority.substr(1, closing - 1));
        if (closing + 1 < authority.size()) {
            if (authority[closing + 1] != ':') {
                throw hackarena3::ConfigError("Invalid HA3_WRAPPER_BACKEND_ENDPOINT URL.");
            }
            port = parse_port_or_default(authority.substr(closing + 2), 443);
        }
    } else {
        const auto colon = authority.find(':');
        if (colon == std::string_view::npos) {
            host = std::string(authority);
        } else {
            if (authority.find(':', colon + 1) != std::string_view::npos) {
                throw hackarena3::ConfigError("Invalid HA3_WRAPPER_BACKEND_ENDPOINT URL.");
            }
            host = std::string(authority.substr(0, colon));
            port = parse_port_or_default(authority.substr(colon + 1), 443);
        }
    }

    if (host.empty()) {
        throw hackarena3::ConfigError(
            "Invalid HA3_WRAPPER_BACKEND_ENDPOINT: missing host in URL."
        );
    }

    while (!path.empty() && path.back() == '/') {
        path.remove_suffix(1);
    }
    if (path.empty() || path == "/") {
        throw hackarena3::ConfigError(
            "Invalid HA3_WRAPPER_BACKEND_ENDPOINT: non-root path prefix is required (for example /backend)."
        );
    }

    return ParsedOfficialEndpoint {
        .grpc_target = format_target(host, port),
        .rpc_prefix = std::string(path),
    };
}

}  // namespace

namespace hackarena3 {

RuntimeConfig load_runtime_config(bool require_api_addr) {
    load_dotenv_if_present();

    RuntimeConfig config;
    if (require_api_addr) {
        config.api_addr = required_api_addr();
    } else {
        config.api_addr = optional_api_addr();
    }

    const auto ha_auth_bin = detail::trim(detail::get_env(kEnvHaAuthBin).value_or(""));
    if (!ha_auth_bin.empty()) {
        config.ha_auth_bin = ha_auth_bin;
    }

    return config;
}

OfficialRuntimeConfig load_official_runtime_config() {
    load_dotenv_if_present();

    const auto endpoint = detail::trim(detail::get_env(kEnvBackendEndpoint).value_or(""));
    if (endpoint.empty()) {
        throw ConfigError(
            std::string("Missing required runtime env: ") + kEnvBackendEndpoint
        );
    }

    const auto team_token = detail::trim(detail::get_env(kEnvTeamToken).value_or(""));
    if (team_token.empty()) {
        throw ConfigError(
            std::string("Missing required runtime env: ") + kEnvTeamToken
        );
    }

    const auto auth_token = detail::trim(detail::get_env(kEnvAuthToken).value_or(""));
    if (auth_token.empty()) {
        throw ConfigError(
            std::string("Missing required runtime env: ") + kEnvAuthToken
        );
    }

    const auto parsed = parse_official_endpoint(endpoint);
    return OfficialRuntimeConfig {
        .grpc_target = parsed.grpc_target,
        .rpc_prefix = parsed.rpc_prefix,
        .team_token = team_token,
        .auth_token = auth_token,
    };
}

}  // namespace hackarena3
