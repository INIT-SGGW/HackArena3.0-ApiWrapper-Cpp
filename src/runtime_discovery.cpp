#include "runtime_discovery.hpp"

#include <chrono>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/client_context.h>

#include "detail/constants.hpp"
#include "detail/grpc_utils.hpp"
#include "detail/path_utils.hpp"
#include "detail/string_utils.hpp"
#include "runtime_common.hpp"
#include "hackarena3/errors.hpp"

namespace {

void add_auth_metadata(grpc::ClientContext& context, const std::string& member_jwt) {
    context.AddMetadata("cookie", "auth_token=" + member_jwt);
}

std::string join_available_sandboxes(
    const std::vector<hackarena3::detail::DiscoveredSandbox>& discovered
) {
    std::ostringstream joined;
    for (std::size_t index = 0; index < discovered.size(); ++index) {
        if (index != 0) {
            joined << ", ";
        }
        joined << discovered[index].sandbox_id;
    }
    return joined.str();
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

std::string random_nonce(std::size_t size) {
    std::random_device random_device;
    std::uniform_int_distribution<int> distribution(0, 255);
    std::string nonce(size, '\0');
    for (char& byte : nonce) {
        byte = static_cast<char>(distribution(random_device));
    }
    return nonce;
}

std::optional<hackarena3::detail::BackendTarget> backend_target_from_endpoint(
    const hackarena::broker::v1::BackendInfo& backend_info,
    const hackarena::broker::v1::Endpoint& endpoint
) {
    const auto host = hackarena3::detail::trim(endpoint.host());
    const auto port = static_cast<int>(endpoint.port());
    if (host.empty() || port <= 0) {
        return std::nullopt;
    }

    return hackarena3::detail::BackendTarget {
        .backend_id = backend_info.backend_id(),
        .user_id = backend_info.user_id(),
        .user_name = hackarena3::detail::trim(backend_info.user_display_name()).empty()
            ? std::nullopt
            : std::make_optional(backend_info.user_display_name()),
        .host = host,
        .port = port,
        .transport = static_cast<int>(endpoint.transport()),
    };
}

bool validate_backend_connection(
    const hackarena3::detail::BackendTarget& backend,
    const std::string& member_jwt
) {
    auto channel = hackarena3::detail::open_insecure_channel(backend.target());
    auto stub = hackarena::connect::v1::ConnectService::NewStub(channel);

    hackarena::connect::v1::ValidateConnectionRequest request;
    request.set_backend_id(backend.backend_id);
    request.set_protocol_version(std::string(hackarena3::detail::kConnectProtocolVersion));
    const auto nonce = random_nonce(16);
    request.set_nonce(nonce);

    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() +
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(hackarena3::detail::kConnectValidateTimeoutSeconds)
        )
    );
    add_auth_metadata(context, member_jwt);

    hackarena::connect::v1::ValidateConnectionResponse response;
    const auto status = stub->ValidateConnection(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "[ha3-wrapper] Endpoint probe failed: " << backend.label()
                  << "; code=" << status_name(status)
                  << "; details="
                  << (status.error_message().empty() ? "no details" : status.error_message())
                  << '\n';
        return false;
    }

    if (response.status() != hackarena::connect::v1::CONNECT_STATUS_OK) {
        std::cerr << "[ha3-wrapper] Endpoint probe rejected: " << backend.label()
                  << "; status="
                  << hackarena::connect::v1::ConnectStatus_Name(response.status())
                  << " message=" << response.message() << '\n';
        return false;
    }
    if (response.backend_id() != backend.backend_id) {
        std::cerr << "[ha3-wrapper] Endpoint probe rejected: " << backend.label()
                  << "; backend_id mismatch (expected=" << backend.backend_id
                  << ", got=" << response.backend_id() << ")\n";
        return false;
    }
    if (response.nonce_echo() != nonce) {
        std::cerr << "[ha3-wrapper] Endpoint probe rejected: " << backend.label()
                  << "; nonce echo mismatch\n";
        return false;
    }

    return true;
}

std::optional<hackarena3::detail::BackendTarget> resolve_reachable_backend(
    const hackarena::broker::v1::BackendInfo& backend_info,
    const std::string& member_jwt
) {
    for (const auto& endpoint : backend_info.endpoints()) {
        auto backend = backend_target_from_endpoint(backend_info, endpoint);
        if (!backend.has_value()) {
            std::cerr << "[ha3-wrapper] Broker endpoint skipped (invalid host/port): user="
                      << backend_info.user_id() << " backend_id=" << backend_info.backend_id()
                      << " host=" << endpoint.host() << " port=" << endpoint.port() << '\n';
            continue;
        }
        if (validate_backend_connection(*backend, member_jwt)) {
            return backend;
        }
    }
    return std::nullopt;
}

std::vector<hackarena3::detail::DiscoveredSandbox> fetch_local_runtime_sandboxes(
    const hackarena3::detail::BackendTarget& backend,
    const std::string& member_jwt
) {
    auto channel = hackarena3::detail::open_insecure_channel(backend.target());
    auto stub = race::v1::LocalSandboxAdminService::NewStub(channel);

    grpc::ClientContext context;
    set_deadline(context);
    add_auth_metadata(context, member_jwt);

    race::v1::GetLocalRuntimeStateRequest request;
    race::v1::GetLocalRuntimeStateResponse response;
    const auto status = stub->GetLocalRuntimeState(&context, request, &response);
    if (!status.ok()) {
        throw hackarena3::RuntimeError(
            "GetLocalRuntimeState failed: " + status_name(status) + " " + status.error_message()
        );
    }

    std::vector<hackarena3::detail::DiscoveredSandbox> discovered;
    discovered.reserve(static_cast<std::size_t>(response.state().active_sandboxes_size()));
    for (const auto& sandbox : response.state().active_sandboxes()) {
        discovered.push_back(hackarena3::detail::DiscoveredSandbox {
            .sandbox_id = sandbox.sandbox_id(),
            .sandbox_name = sandbox.sandbox_name(),
            .map_id = sandbox.map_id(),
            .active_player_count = static_cast<int>(sandbox.active_player_count()),
            .backend = backend,
        });
    }
    return discovered;
}

}  // namespace

namespace hackarena3::detail {

std::string BackendTarget::target() const {
    if (host.find(':') != std::string::npos && !host.starts_with('[')) {
        return "[" + host + "]:" + std::to_string(port);
    }
    return host + ":" + std::to_string(port);
}

std::string BackendTarget::label() const {
    return user_id + "/" + backend_id + "/" + host + ":" + std::to_string(port);
}

std::string BackendTarget::user_display() const {
    const auto value = user_name.has_value() ? trim(*user_name) : "";
    return value.empty() ? "-" : value;
}

BrokerApi create_broker_api(const hackarena3::RuntimeConfig& config) {
    const auto parsed = parse_api_addr(config.api_addr);
    const auto resolved = resolve_target(parsed);
    auto channel = open_secure_channel(parsed, resolved);
    return BrokerApi {
        .channel = channel,
        .target = resolved.dial_target,
    };
}

std::vector<DiscoveredSandbox> discover_team_sandboxes(
    BrokerApi& broker_api,
    const std::string& member_jwt
) {
    std::cerr << "[ha3-wrapper] Fetching team backends via BrokerService...\n";

    hackarena::broker::v1::GetTeamBackendsResponse response;
    grpc::Status status;
    bool received_response = false;

    for (std::size_t attempt = 0;; ++attempt) {
        grpc::ClientContext broker_context;
        set_deadline(broker_context);
        add_auth_metadata(broker_context, member_jwt);

        hackarena::broker::v1::GetTeamBackendsRequest request;
        response.Clear();
        status = unary_rpc_call(
            broker_api.channel,
            broker_context,
            detail::kBrokerGetTeamBackendsMethod,
            request,
            response
        );
        if (status.ok()) {
            received_response = true;
            break;
        }

        const auto transient =
            status.error_code() == grpc::StatusCode::UNAVAILABLE ||
            status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED;
        if (!transient || attempt >= detail::kRetryBackoffSeconds.size()) {
            break;
        }

        const auto delay_seconds = detail::kRetryBackoffSeconds[attempt];
        std::cerr << "[ha3-wrapper] GetTeamBackends transient failure: code="
                  << status_name(status)
                  << " details="
                  << (status.error_message().empty() ? "no details" : status.error_message())
                  << " retry_in_s=" << delay_seconds
                  << " attempt=" << (attempt + 1) << "/" << detail::kRetryBackoffSeconds.size()
                  << '\n';
        std::this_thread::sleep_for(std::chrono::duration<double>(delay_seconds));
    }

    if (!received_response) {
        throw RuntimeError(
            "GetTeamBackends failed: " + status_name(status) + " " +
            (status.error_message().empty() ? "no details" : status.error_message())
        );
    }
    if (response.backends().empty()) {
        throw RuntimeError("Broker returned no team backends.");
    }

    std::vector<DiscoveredSandbox> discovered;
    for (const auto& backend_info : response.backends()) {
        auto backend = resolve_reachable_backend(backend_info, member_jwt);
        if (!backend.has_value()) {
            std::cerr << "[ha3-wrapper] Broker backend skipped (no reachable endpoint after probe): user="
                      << backend_info.user_id() << " backend_id=" << backend_info.backend_id()
                      << '\n';
            continue;
        }

        try {
            auto sandboxes = fetch_local_runtime_sandboxes(*backend, member_jwt);
            discovered.insert(discovered.end(), sandboxes.begin(), sandboxes.end());
        } catch (const RuntimeError& exc) {
            std::cerr << "[ha3-wrapper] Backend skipped (GetLocalRuntimeState failed): "
                      << backend->label() << "; details=" << exc.what() << '\n';
        } catch (const std::exception& exc) {
            std::cerr << "[ha3-wrapper] Backend skipped (runtime fetch error): "
                      << backend->label() << "; details=" << exc.what() << '\n';
        }
    }

    if (discovered.empty()) {
        throw RuntimeError("No active sandboxes found in team backends.");
    }

    return discovered;
}

DiscoveredSandbox choose_sandbox(
    const std::vector<DiscoveredSandbox>& discovered,
    const std::optional<std::string>& sandbox_id
) {
    const auto configured_sandbox_id = trim(sandbox_id.value_or(""));
    if (!configured_sandbox_id.empty()) {
        for (const auto& item : discovered) {
            if (item.sandbox_id == configured_sandbox_id) {
                std::cerr << "[ha3-wrapper] Using sandbox selected by --sandbox_id: "
                          << item.sandbox_id << " (" << item.backend.label() << ")\n";
                return item;
            }
        }
        throw RuntimeError(
            "--sandbox_id=" + configured_sandbox_id +
            " not found in active team sandboxes. Available sandbox IDs: " +
            join_available_sandboxes(discovered)
        );
    }

    std::cout << "[ha3-wrapper] Active team sandboxes (broker):\n";
    for (std::size_t index = 0; index < discovered.size(); ++index) {
        const auto& entry = discovered[index];
        std::cout << "[ha3-wrapper] " << (index + 1) << ". " << entry.sandbox_name
                  << " | id=" << entry.sandbox_id
                  << " | user=" << entry.backend.user_display()
                  << " | map=" << entry.map_id
                  << " | players=" << entry.active_player_count
                  << " | endpoint=" << entry.backend.host << ":" << entry.backend.port << '\n';
    }

    if (!stdin_is_tty()) {
        throw RuntimeError(
            "Non-interactive mode requires --sandbox_id. Available sandbox IDs: " +
            join_available_sandboxes(discovered)
        );
    }

    for (;;) {
        std::cout << "Select sandbox [1-" << discovered.size() << "] (default 1): ";
        std::string raw;
        if (!std::getline(std::cin, raw)) {
            throw RuntimeError("Sandbox selection aborted.");
        }
        raw = trim(raw);
        if (raw.empty()) {
            return discovered.front();
        }

        try {
            const auto index = static_cast<std::size_t>(std::stoul(raw));
            if (index >= 1 && index <= discovered.size()) {
                return discovered[index - 1];
            }
        } catch (const std::exception&) {
        }
        std::cout << "[ha3-wrapper] Invalid selection. Try again.\n";
    }
}

}  // namespace hackarena3::detail
