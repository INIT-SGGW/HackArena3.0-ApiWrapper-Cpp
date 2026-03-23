#include "game_token.hpp"

#include <chrono>
#include <iostream>
#include <vector>

#include <grpcpp/client_context.h>

#include "auth/v1/game_token_issuer.pb.h"
#include "detail/constants.hpp"
#include "detail/grpc_utils.hpp"
#include "detail/string_utils.hpp"
#include "runtime_common.hpp"
#include "hackarena3/errors.hpp"

namespace {

void add_metadata(
    grpc::ClientContext& context,
    const std::vector<std::pair<std::string, std::string>>& metadata
) {
    for (const auto& [key, value] : metadata) {
        context.AddMetadata(key, value);
    }
}

std::string status_name(const grpc::Status& status) {
    return std::string(hackarena3::detail::grpc_status_code_name(
        static_cast<grpc::StatusCode>(status.error_code())
    ));
}

}  // namespace

namespace hackarena3::detail {

GameTokenProvider::GameTokenProvider(std::string api_addr, std::string member_jwt)
    : member_jwt_(std::move(member_jwt)) {
    if (trim(member_jwt_).empty()) {
        throw GameTokenError("member_jwt is empty; cannot request game token.");
    }

    const auto target = parse_api_addr(api_addr);
    target_ = target.target();
    channel_ = open_secure_channel(target);
}

GameTokenProvider::~GameTokenProvider() = default;

std::int64_t GameTokenProvider::now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

GameToken GameTokenProvider::request_game_token() {
    auth::v1::IssueGameTokenRequest request;
    request.set_token_type(auth::v1::GAME_TOKEN_ISSUE_TYPE_TEAM_BOT_DEV);

    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + std::chrono::seconds(detail::kRpcTimeoutSeconds)
    );
    add_metadata(
        context,
        {
            {"authorization", "Bearer " + member_jwt_},
            {"cookie", "auth_token=" + member_jwt_},
        }
    );

    if (!request_info_logged_) {
        std::cerr << "[ha3-wrapper] Requesting game token via gRPC: target=" << target_
                  << " method=" << detail::kIssueGameTokenMethod << '\n';
        request_info_logged_ = true;
    }

    auth::v1::IssueGameTokenResponse response;
    const auto status = unary_rpc_call(
        channel_,
        context,
        detail::kIssueGameTokenMethod,
        request,
        response
    );
    if (!status.ok()) {
        if (status.error_code() == grpc::StatusCode::UNIMPLEMENTED) {
            throw GameTokenError("Game token service unavailable (UNIMPLEMENTED).");
        }
        throw GameTokenError(
            "Game token gRPC request failed: " + std::string(detail::kIssueGameTokenMethod) + "; code=" +
            status_name(status) + "; details=" +
            (status.error_message().empty() ? "no details" : status.error_message())
        );
    }

    const auto& token = response.token();
    if (trim(token.jwt()).empty()) {
        throw GameTokenError("Game token gRPC response has empty `token.jwt`.");
    }
    if (token.exp_utc().seconds() <= 0) {
        throw GameTokenError("Game token response is missing a valid token.exp_utc timestamp.");
    }

    return GameToken {
        .token = token.jwt(),
        .exp = token.exp_utc().seconds(),
        .kid = trim(token.kid()).empty() ? std::nullopt : std::make_optional(token.kid()),
    };
}

GameToken GameTokenProvider::refresh() {
    current_ = request_game_token();
    return *current_;
}

const GameToken& GameTokenProvider::get() {
    if (!current_.has_value()) {
        current_ = request_game_token();
    }
    return *current_;
}

bool GameTokenProvider::ensure_fresh(int refresh_skew_seconds) {
    const auto& token = get();
    if (now_epoch_seconds() >= token.exp - refresh_skew_seconds) {
        const auto previous_token = token.token;
        const auto refreshed = refresh();
        return refreshed.token != previous_token;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> GameTokenProvider::grpc_metadata() {
    return {{"x-ha3-game-token", get().token}};
}

std::vector<std::pair<std::string, std::string>> GameTokenProvider::member_auth_metadata() const {
    return {{"cookie", "auth_token=" + member_jwt_}};
}

}  // namespace hackarena3::detail
