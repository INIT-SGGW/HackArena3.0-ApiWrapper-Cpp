#pragma once

#include <optional>

#include "hackarena3/config.hpp"
#include "hackarena3/types.hpp"

namespace hackarena3::detail {

void run_runtime(
    BotProtocol& bot,
    const RuntimeConfig& config,
    const std::optional<OfficialRuntimeConfig>& official_config = std::nullopt
);

}  // namespace hackarena3::detail
