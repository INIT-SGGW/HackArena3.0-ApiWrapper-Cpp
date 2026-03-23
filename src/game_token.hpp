#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <grpcpp/channel.h>

namespace hackarena3::detail {

struct GameToken {
    std::string token;
    std::int64_t exp {};
    std::optional<std::string> kid;
};

class GameTokenProvider {
public:
    GameTokenProvider(std::string api_addr, std::string member_jwt);
    ~GameTokenProvider();

    GameTokenProvider(const GameTokenProvider&) = delete;
    GameTokenProvider& operator=(const GameTokenProvider&) = delete;

    GameToken refresh();
    const GameToken& get();
    bool ensure_fresh(int refresh_skew_seconds = 30);

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> grpc_metadata();
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> member_auth_metadata() const;

private:
    GameToken request_game_token();
    static std::int64_t now_epoch_seconds();

    std::string member_jwt_;
    std::string target_;
    std::shared_ptr<grpc::Channel> channel_;
    bool request_info_logged_ {false};
    std::optional<GameToken> current_;
};

}  // namespace hackarena3::detail
