#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace hackarena3::detail {

inline constexpr std::uint32_t kRequestedHz = 60;
inline constexpr int kRpcTimeoutSeconds = 10;
inline constexpr double kConnectValidateTimeoutSeconds = 2.0;
inline constexpr std::chrono::milliseconds kRuntimePollInterval {200};
inline constexpr int kTokenRefreshSkewSeconds = 30;
inline constexpr std::array<double, 3> kRetryBackoffSeconds {1.0, 2.0, 4.0};
inline constexpr std::string_view kConnectProtocolVersion = "1";
inline constexpr std::string_view kBrokerGetTeamBackendsMethod =
    "/broker/hackarena.broker.v1.BrokerService/GetTeamBackends";
inline constexpr std::string_view kIssueGameTokenMethod =
    "/gametoken/auth.v1.GameTokenIssuerService/IssueGameToken";

}  // namespace hackarena3::detail
