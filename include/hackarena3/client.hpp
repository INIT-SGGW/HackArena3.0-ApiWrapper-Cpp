#pragma once

#include <optional>

#include "hackarena3/config.hpp"
#include "hackarena3/types.hpp"

namespace hackarena3 {

int run_bot(BotProtocol& bot, std::optional<RuntimeConfig> config = std::nullopt);
int run_bot(
    BotProtocol& bot,
    int argc,
    char** argv,
    std::optional<RuntimeConfig> config = std::nullopt
);

}  // namespace hackarena3
