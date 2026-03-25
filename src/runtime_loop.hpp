#pragma once

#include <functional>
#include <optional>

#include "hackarena3/types.hpp"
#include "game_token.hpp"
#include "runtime_race.hpp"

namespace hackarena3::detail {

using StreamMetadataProvider =
    std::function<std::vector<std::pair<std::string, std::string>>()>;

void run_participant_loop(
    BotProtocol& bot,
    RaceApi& api,
    BotContext& ctx,
    const StreamMetadataProvider& metadata_provider,
    GameTokenProvider* token_provider = nullptr,
    bool allow_auth_refresh = true,
    const std::optional<std::string>& stream_method = std::nullopt,
    const std::optional<std::string>& expected_map_id = std::nullopt
);

}  // namespace hackarena3::detail
