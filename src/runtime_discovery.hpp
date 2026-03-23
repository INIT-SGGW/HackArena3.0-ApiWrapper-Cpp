#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <grpcpp/channel.h>

#include "hackarena/broker/v1/broker.pb.h"
#include "hackarena/connect/v1/connect.grpc.pb.h"
#include "race/v1/runtime_local.grpc.pb.h"

#include "hackarena3/config.hpp"

namespace hackarena3::detail {

struct BackendTarget {
    std::string backend_id;
    std::string user_id;
    std::optional<std::string> user_name;
    std::string host;
    int port {};
    int transport {};

    [[nodiscard]] std::string target() const;
    [[nodiscard]] std::string label() const;
    [[nodiscard]] std::string user_display() const;
};

struct DiscoveredSandbox {
    std::string sandbox_id;
    std::string sandbox_name;
    std::string map_id;
    int active_player_count {};
    BackendTarget backend;
};

struct BrokerApi {
    std::shared_ptr<grpc::Channel> channel;
    std::string target;
};

BrokerApi create_broker_api(const hackarena3::RuntimeConfig& config);
std::vector<DiscoveredSandbox> discover_team_sandboxes(
    BrokerApi& broker_api,
    const std::string& member_jwt
);
DiscoveredSandbox choose_sandbox(
    const std::vector<DiscoveredSandbox>& discovered,
    const std::optional<std::string>& sandbox_id
);

}  // namespace hackarena3::detail
