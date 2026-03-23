#include "auth.hpp"

#include <filesystem>
#include <optional>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "detail/environment.hpp"
#include "detail/path_utils.hpp"
#include "detail/process.hpp"
#include "detail/string_utils.hpp"
#include "hackarena3/config.hpp"
#include "hackarena3/errors.hpp"

namespace {

std::string login_hint(const std::string& binary) {
    return "Run `hackarena auth login` or `" + binary + " login`.";
}

std::string join_command(const std::filesystem::path& binary, const std::vector<std::string>& args) {
    std::ostringstream command;
    command << binary.string();
    for (const auto& arg : args) {
        command << ' ' << arg;
    }
    return command.str();
}

std::optional<std::filesystem::path> resolve_from_candidate(const std::optional<std::string>& candidate) {
    if (!candidate.has_value()) {
        return std::nullopt;
    }
    const auto trimmed = hackarena3::detail::trim(*candidate);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    return hackarena3::detail::resolve_executable_candidate(trimmed);
}

struct HaAuthJsonResult {
    nlohmann::json payload;
    int exit_code {};
    std::string stderr_text;
    bool has_payload {};
};

HaAuthJsonResult run_ha_auth_json(
    const std::filesystem::path& binary,
    const std::vector<std::string>& args
) {
    hackarena3::detail::ProcessResult result;
    try {
        result = hackarena3::detail::run_process(binary, args);
    } catch (const hackarena3::RuntimeError&) {
        throw;
    } catch (const std::exception& exc) {
        throw hackarena3::AuthError(
            "Failed to run `" + binary.string() + "`: " + std::string(exc.what())
        );
    }

    const auto command_display = join_command(binary, args);
    const auto stdout_text = hackarena3::detail::trim(result.stdout_text);
    const auto stderr_text = hackarena3::detail::trim(result.stderr_text);

    if (stdout_text.empty()) {
        if (result.exit_code != 0) {
            return HaAuthJsonResult {
                .payload = nlohmann::json::object(),
                .exit_code = result.exit_code,
                .stderr_text = stderr_text,
                .has_payload = false,
            };
        }
        throw hackarena3::AuthError("`" + command_display + "` returned empty stdout.");
    }

    try {
        return HaAuthJsonResult {
            .payload = nlohmann::json::parse(stdout_text),
            .exit_code = result.exit_code,
            .stderr_text = stderr_text,
            .has_payload = true,
        };
    } catch (const nlohmann::json::parse_error&) {
        if (result.exit_code != 0) {
            return HaAuthJsonResult {
                .payload = nlohmann::json::object(),
                .exit_code = result.exit_code,
                .stderr_text = stderr_text,
                .has_payload = false,
            };
        }
        throw hackarena3::AuthError("`" + command_display + "` did not return valid JSON.");
    }
}

}  // namespace

namespace hackarena3::detail {

std::string resolve_ha_auth_binary(const std::optional<std::string>& ha_auth_bin) {
    std::vector<std::optional<std::string>> candidates;
    candidates.push_back(ha_auth_bin);
    candidates.push_back(get_env(kEnvHaAuthBin));

    const auto local_app_data = trim(get_env("LOCALAPPDATA").value_or(""));
    if (!local_app_data.empty()) {
        candidates.push_back(
            (std::filesystem::path(local_app_data) / "HackArena" / "bin" / "ha-auth.exe").string()
        );
    }

    auto xdg_data_home = get_env("XDG_DATA_HOME");
    if (!xdg_data_home.has_value() || trim(*xdg_data_home).empty()) {
        xdg_data_home = expand_user_path("~/.local/share").string();
    }
    if (xdg_data_home.has_value() && !trim(*xdg_data_home).empty()) {
        candidates.push_back((std::filesystem::path(*xdg_data_home) / "hackarena" / "bin" / "ha-auth").string());
    }
    candidates.push_back(expand_user_path("~/.local/share/hackarena/bin/ha-auth").string());
    candidates.push_back("ha-auth");

    for (const auto& candidate : candidates) {
        if (auto resolved = resolve_from_candidate(candidate)) {
            return resolved->string();
        }
    }

    throw AuthError(
        "Cannot find `ha-auth` binary. Run `hackarena install auth` or set HA3_WRAPPER_HA_AUTH_BIN."
    );
}

std::string fetch_member_jwt(const std::optional<std::string>& ha_auth_bin) {
    const auto binary = std::filesystem::path(resolve_ha_auth_binary(ha_auth_bin));
    const auto result = run_ha_auth_json(binary, {"token", "-q"});

    if (result.exit_code == 2) {
        throw AuthError("Auth login required. " + login_hint(binary.string()));
    }
    if (result.exit_code != 0) {
        const auto details = result.stderr_text.empty() ? "" : " stderr: " + result.stderr_text;
        throw AuthError(
            "Auth token retrieval failed with exit code " + std::to_string(result.exit_code) + ". " +
            login_hint(binary.string()) + " Check auth CLI diagnostics." + details
        );
    }
    if (!result.has_payload) {
        throw AuthError("Auth token response is missing JSON payload.");
    }

    const auto token_it = result.payload.find("token");
    if (token_it == result.payload.end() || !token_it->is_string()) {
        throw AuthError("Auth token response is missing `token` field.");
    }

    const auto jwt = trim(token_it->get<std::string>());
    if (jwt.empty()) {
        throw AuthError("Auth token response is missing `token` field.");
    }
    return jwt;
}

}  // namespace hackarena3::detail
