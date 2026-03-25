#pragma once

#include <grpcpp/channel.h>

#include <memory>
#include <string>
#include <vector>

namespace hackarena3::detail {

struct ParsedApiTarget {
    std::string host;
    int port {};

    [[nodiscard]] std::string authority() const;
    [[nodiscard]] std::string target() const;
};

struct ResolvedTarget {
    std::string authority_target;
    std::string dial_target;
    std::vector<std::string> resolved_addresses;
};

ParsedApiTarget parse_api_addr(const std::string& api_addr);
ResolvedTarget resolve_target(const ParsedApiTarget& target);
std::shared_ptr<grpc::Channel> open_secure_channel(const ParsedApiTarget& target);
std::shared_ptr<grpc::Channel> open_secure_channel(
    const ParsedApiTarget& target,
    const ResolvedTarget& resolved
);
std::shared_ptr<grpc::Channel> open_secure_channel(const std::string& target);
std::shared_ptr<grpc::Channel> open_insecure_channel(const std::string& target);

}  // namespace hackarena3::detail
