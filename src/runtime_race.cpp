#include "runtime_race.hpp"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <grpcpp/client_context.h>
#include <grpcpp/impl/client_unary_call.h>
#include <grpcpp/impl/rpc_method.h>
#include <grpcpp/support/sync_stream.h>

#include "detail/constants.hpp"
#include "detail/grpc_utils.hpp"
#include "detail/string_utils.hpp"
#include "game_token.hpp"
#include "runtime_common.hpp"
#include "hackarena3/errors.hpp"

namespace {

constexpr char kPrepareOfficialJoinSuffix[] =
    "/race.v1.RaceParticipantService/PrepareOfficialJoin";
constexpr char kGetTrackDataSuffix[] = "/race.v1.TrackService/GetTrackData";

void add_metadata(
    grpc::ClientContext& context,
    const std::vector<std::pair<std::string, std::string>>& metadata
) {
    for (const auto& [key, value] : metadata) {
        context.AddMetadata(key, value);
    }
}

void set_deadline(grpc::ClientContext& context) {
    context.set_deadline(
        std::chrono::system_clock::now() +
        std::chrono::seconds(hackarena3::detail::kRpcTimeoutSeconds)
    );
}

std::string status_name(const grpc::Status& status) {
    return std::string(hackarena3::detail::grpc_status_code_name(
        static_cast<grpc::StatusCode>(status.error_code())
    ));
}

std::string prefixed_method(const std::string& rpc_prefix, std::string_view suffix) {
    auto prefix = hackarena3::detail::trim(rpc_prefix);
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    if (prefix.empty() || prefix == "/") {
        throw hackarena3::RuntimeError("Official rpc_prefix is empty; cannot build RPC method.");
    }
    if (!prefix.starts_with('/')) {
        prefix.insert(prefix.begin(), '/');
    }
    return prefix + std::string(suffix);
}

}  // namespace

namespace hackarena3::detail {

RaceApi create_backend_api(const BackendTarget& backend) {
    auto channel = open_insecure_channel(backend.target());
    return RaceApi {
        .channel = channel,
        .race = race::v1::RaceService::NewStub(channel),
        .participant = race::v1::RaceParticipantService::NewStub(channel),
        .track = race::v1::TrackService::NewStub(channel),
    };
}

RaceApi create_official_backend_api(const std::string& grpc_target) {
    auto channel = open_secure_channel(grpc_target);
    return RaceApi {
        .channel = channel,
        .race = race::v1::RaceService::NewStub(channel),
        .participant = race::v1::RaceParticipantService::NewStub(channel),
        .track = race::v1::TrackService::NewStub(channel),
    };
}

std::vector<std::pair<std::string, std::string>> race_metadata(GameTokenProvider& token_provider) {
    auto metadata = token_provider.member_auth_metadata();
    auto token_metadata = token_provider.grpc_metadata();
    metadata.insert(metadata.end(), token_metadata.begin(), token_metadata.end());
    return metadata;
}

std::vector<std::pair<std::string, std::string>> race_metadata_official(
    const std::string& team_token,
    const std::string& auth_token
) {
    const auto game_token = hackarena3::detail::trim(team_token);
    if (game_token.empty()) {
        throw RuntimeError("Team token is empty; cannot build stream metadata.");
    }

    const auto member_auth_token = hackarena3::detail::trim(auth_token);
    if (member_auth_token.empty()) {
        throw RuntimeError("Auth token is empty; cannot build stream metadata.");
    }

    return {
        {"x-ha3-game-token", game_token},
        {"cookie", "auth_token=" + member_auth_token},
    };
}

race::v1::PrepareOfficialJoinResponse prepare_official_join(
    RaceApi& api,
    const std::string& rpc_prefix,
    const std::vector<std::pair<std::string, std::string>>& metadata
) {
    grpc::ClientContext context;
    set_deadline(context);
    add_metadata(context, metadata);

    const auto method_name = prefixed_method(rpc_prefix, kPrepareOfficialJoinSuffix);
    grpc::internal::RpcMethod method(
        method_name.c_str(),
        grpc::internal::RpcMethod::NORMAL_RPC,
        api.channel
    );

    race::v1::PrepareOfficialJoinRequest request;
    race::v1::PrepareOfficialJoinResponse response;
    const auto status = grpc::internal::BlockingUnaryCall<
        race::v1::PrepareOfficialJoinRequest,
        race::v1::PrepareOfficialJoinResponse,
        grpc::protobuf::MessageLite,
        grpc::protobuf::MessageLite>(api.channel.get(), method, &context, request, &response);

    if (!status.ok()) {
        if (status.error_code() == grpc::StatusCode::UNIMPLEMENTED) {
            throw RuntimeError(
                "PrepareOfficialJoin unavailable (UNIMPLEMENTED). Official mode requires backend with PrepareOfficialJoin support."
            );
        }
        throw RuntimeError(
            "PrepareOfficialJoin failed: " + status_name(status) + " " + status.error_message()
        );
    }
    if (hackarena3::detail::trim(response.map_id()).empty()) {
        throw RuntimeError(
            "PrepareOfficialJoin returned empty map_id; cannot preload TrackData."
        );
    }
    return response;
}

race::v1::TrackData fetch_track_data(
    RaceApi& api,
    GameTokenProvider& token_provider,
    const std::string& map_id
) {
    if (hackarena3::detail::trim(map_id).empty()) {
        throw RuntimeError("LocalSandboxJoin returned empty map_id; cannot fetch track data.");
    }

    grpc::ClientContext context;
    set_deadline(context);
    add_metadata(context, race_metadata(token_provider));

    race::v1::GetTrackDataRequest request;
    request.set_map_id(map_id);
    race::v1::GetTrackDataResponse response;
    const auto status = api.track->GetTrackData(&context, request, &response);
    if (!status.ok()) {
        throw RuntimeError("GetTrackData failed: " + status_name(status) + " " + status.error_message());
    }
    return response.track();
}

race::v1::TrackData fetch_track_data_official(
    RaceApi& api,
    const std::string& rpc_prefix,
    const std::vector<std::pair<std::string, std::string>>& metadata,
    const std::string& map_id
) {
    if (trim(map_id).empty()) {
        throw RuntimeError("PrepareOfficialJoin returned empty map_id; cannot fetch track data.");
    }

    grpc::ClientContext context;
    set_deadline(context);
    add_metadata(context, metadata);

    const auto method_name = prefixed_method(rpc_prefix, kGetTrackDataSuffix);
    grpc::internal::RpcMethod method(
        method_name.c_str(),
        grpc::internal::RpcMethod::NORMAL_RPC,
        api.channel
    );

    race::v1::GetTrackDataRequest request;
    request.set_map_id(map_id);
    race::v1::GetTrackDataResponse response;
    const auto status = grpc::internal::BlockingUnaryCall<
        race::v1::GetTrackDataRequest,
        race::v1::GetTrackDataResponse,
        grpc::protobuf::MessageLite,
        grpc::protobuf::MessageLite>(api.channel.get(), method, &context, request, &response);

    if (!status.ok()) {
        throw RuntimeError(
            "GetTrackData (official) failed: " + status_name(status) + " " +
            status.error_message()
        );
    }
    return response.track();
}

}  // namespace hackarena3::detail
