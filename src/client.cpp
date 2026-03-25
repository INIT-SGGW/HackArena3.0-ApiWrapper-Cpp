#include "hackarena3/client.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "detail/string_utils.hpp"
#include "runtime.hpp"
#include "hackarena3/errors.hpp"

namespace {

struct CliOverrides {
    std::optional<std::string> sandbox_id;
    bool official {false};
};

CliOverrides parse_cli_overrides(int argc, char** argv) {
    CliOverrides overrides;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--official") {
            overrides.official = true;
            continue;
        }
        if (arg == "--sandbox_id") {
            if (index + 1 >= argc) {
                throw hackarena3::ConfigError("Empty value for --sandbox_id.");
            }
            const auto value = hackarena3::detail::trim(argv[++index]);
            if (value.empty()) {
                throw hackarena3::ConfigError("Empty value for --sandbox_id.");
            }
            overrides.sandbox_id = value;
            continue;
        }
        constexpr std::string_view prefix = "--sandbox_id=";
        if (arg.starts_with(prefix)) {
            const auto value = hackarena3::detail::trim(arg.substr(prefix.size()));
            if (value.empty()) {
                throw hackarena3::ConfigError("Empty value for --sandbox_id.");
            }
            overrides.sandbox_id = value;
        }
    }
    if (overrides.official && overrides.sandbox_id.has_value()) {
        throw hackarena3::ConfigError(
            "Conflicting CLI flags: --official cannot be used together with --sandbox_id."
        );
    }
    return overrides;
}

int run_bot_impl(
    hackarena3::BotProtocol& bot,
    std::optional<hackarena3::RuntimeConfig> config,
    const CliOverrides& cli_overrides
) {
    try {
        auto runtime_config = config.has_value()
            ? *config
            : hackarena3::load_runtime_config(!cli_overrides.official);
        std::optional<hackarena3::OfficialRuntimeConfig> official_config;
        if (cli_overrides.official) {
            official_config = hackarena3::load_official_runtime_config();
        }
        if (cli_overrides.sandbox_id.has_value()) {
            runtime_config.sandbox_id = cli_overrides.sandbox_id;
        }
        hackarena3::detail::run_runtime(bot, runtime_config, official_config);
        return 0;
    } catch (const hackarena3::ConfigError& exc) {
        std::cerr << "[ha3-wrapper] " << exc.what() << '\n';
        return 1;
    } catch (const hackarena3::RuntimeError& exc) {
        std::cerr << "[ha3-wrapper] " << exc.what() << '\n';
        return 1;
    } catch (const std::exception& exc) {
        std::cerr << "[ha3-wrapper] Unexpected error: " << exc.what() << '\n';
        return 1;
    }
}

}  // namespace

namespace hackarena3 {

int run_bot(BotProtocol& bot, std::optional<RuntimeConfig> config) {
    return run_bot_impl(bot, std::move(config), CliOverrides {});
}

int run_bot(BotProtocol& bot, int argc, char** argv, std::optional<RuntimeConfig> config) {
    return run_bot_impl(bot, std::move(config), parse_cli_overrides(argc, argv));
}

}  // namespace hackarena3
