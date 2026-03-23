#pragma once

#include "hackarena3/types.hpp"
#include "game_token.hpp"
#include "runtime_race.hpp"

namespace hackarena3::detail {

void run_participant_loop(
    BotProtocol& bot,
    RaceApi& api,
    GameTokenProvider& token_provider,
    BotContext& ctx
);

}  // namespace hackarena3::detail
