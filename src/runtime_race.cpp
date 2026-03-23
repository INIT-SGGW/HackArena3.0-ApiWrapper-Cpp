#include "runtime_race.hpp"

#include <chrono>
#include <string>
#include <vector>

#include <grpcpp/client_context.h>

#include "detail/constants.hpp"
#include "detail/grpc_utils.hpp"
#include "detail/string_utils.hpp"
#include "game_token.hpp"
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

std::vector<std::pair<std::string, std::string>> race_metadata(GameTokenProvider& token_provider) {
    auto metadata = token_provider.member_auth_metadata();
    auto token_metadata = token_provider.grpc_metadata();
    metadata.insert(metadata.end(), token_metadata.begin(), token_metadata.end());
    return metadata;
}

race::v1::TrackData fetch_track_data(
    RaceApi& api,
    GameTokenProvider& token_provider,
    const std::string& map_id
) {
    if (trim(map_id).empty()) {
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

}  // namespace hackarena3::detail
