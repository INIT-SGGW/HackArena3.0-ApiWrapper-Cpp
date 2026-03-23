#pragma once

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/channel.h>

#include "race/v1/race.grpc.pb.h"
#include "race/v1/track.pb.h"
#include "race/v1/track.grpc.pb.h"

#include "runtime_discovery.hpp"

namespace hackarena3::detail {

class GameTokenProvider;

struct RaceApi {
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<race::v1::RaceService::Stub> race;
    std::unique_ptr<race::v1::RaceParticipantService::Stub> participant;
    std::unique_ptr<race::v1::TrackService::Stub> track;
};

RaceApi create_backend_api(const BackendTarget& backend);
std::vector<std::pair<std::string, std::string>> race_metadata(GameTokenProvider& token_provider);
race::v1::TrackData fetch_track_data(
    RaceApi& api,
    GameTokenProvider& token_provider,
    const std::string& map_id
);

}  // namespace hackarena3::detail
