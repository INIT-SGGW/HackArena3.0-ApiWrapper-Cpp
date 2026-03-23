#include "runtime_common.hpp"

#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <string_view>

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "hackarena3/errors.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace {

std::string format_target(std::string_view host, int port) {
    if (host.find(':') != std::string_view::npos && !host.starts_with('[')) {
        return "[" + std::string(host) + "]:" + std::to_string(port);
    }
    return std::string(host) + ":" + std::to_string(port);
}

std::string_view authority_of(std::string_view api_addr) {
    constexpr std::string_view prefix = "https://";
    if (!api_addr.starts_with(prefix)) {
        throw hackarena3::RuntimeError("Invalid api_addr URL scheme. Expected https://.");
    }

    auto authority = api_addr.substr(prefix.size());
    const auto slash_pos = authority.find_first_of("/?#");
    if (slash_pos != std::string_view::npos) {
        authority = authority.substr(0, slash_pos);
    }
    if (authority.empty()) {
        throw hackarena3::RuntimeError("Invalid api_addr URL.");
    }
    return authority;
}

int parse_port_or_default(const std::string& value, int default_port) {
    if (value.empty()) {
        return default_port;
    }
    try {
        const int port = std::stoi(value);
        if (port <= 0 || port > 65535) {
            throw std::out_of_range("port");
        }
        return port;
    } catch (const std::exception&) {
        throw hackarena3::RuntimeError("Invalid api_addr port in URL.");
    }
}

#ifdef _WIN32
void ensure_socket_api_ready() {
    static const bool initialized = [] {
        WSADATA data {};
        const auto result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            throw hackarena3::RuntimeError(
                "WSAStartup failed while resolving api_addr host."
            );
        }
        return true;
    }();
    (void)initialized;
}
#else
void ensure_socket_api_ready() {}
#endif

std::vector<std::string> resolve_numeric_addresses(std::string_view host, int port) {
    ensure_socket_api_ready();

    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* raw_results = nullptr;
    const auto service = std::to_string(port);
    const auto result = getaddrinfo(
        std::string(host).c_str(),
        service.c_str(),
        &hints,
        &raw_results
    );
    if (result != 0) {
#ifdef _WIN32
        const auto* error_message = gai_strerrorA(result);
#else
        const auto* error_message = gai_strerror(result);
#endif
        throw hackarena3::RuntimeError(
            "System resolver failed for api_addr host '" + std::string(host) +
            "': " + std::string(error_message == nullptr ? "unknown error" : error_message)
        );
    }

    std::set<std::string> deduplicated;
    for (auto* current = raw_results; current != nullptr; current = current->ai_next) {
        char numeric_host[NI_MAXHOST] {};
        const auto name_info = getnameinfo(
            current->ai_addr,
            static_cast<socklen_t>(current->ai_addrlen),
            numeric_host,
            static_cast<socklen_t>(sizeof(numeric_host)),
            nullptr,
            0,
            NI_NUMERICHOST
        );
        if (name_info == 0) {
            deduplicated.insert(numeric_host);
        }
    }

    freeaddrinfo(raw_results);

    if (deduplicated.empty()) {
        throw hackarena3::RuntimeError(
            "System resolver returned no numeric addresses for api_addr host '" +
            std::string(host) + "'."
        );
    }

    return std::vector<std::string>(deduplicated.begin(), deduplicated.end());
}

}  // namespace

namespace hackarena3::detail {

std::string ParsedApiTarget::authority() const {
    return format_target(host, port);
}

std::string ParsedApiTarget::target() const {
    return authority();
}

ParsedApiTarget parse_api_addr(const std::string& api_addr) {
    const auto authority = authority_of(api_addr);
    ParsedApiTarget parsed;

    if (authority.starts_with('[')) {
        const auto closing = authority.find(']');
        if (closing == std::string_view::npos) {
            throw RuntimeError("Invalid api_addr URL.");
        }
        parsed.host = std::string(authority.substr(1, closing - 1));
        if (closing + 1 < authority.size()) {
            if (authority[closing + 1] != ':') {
                throw RuntimeError("Invalid api_addr URL.");
            }
            parsed.port = parse_port_or_default(std::string(authority.substr(closing + 2)), 443);
        } else {
            parsed.port = 443;
        }
    } else {
        const auto colon = authority.find(':');
        if (colon == std::string_view::npos) {
            parsed.host = std::string(authority);
            parsed.port = 443;
        } else {
            if (authority.find(':', colon + 1) != std::string_view::npos) {
                throw RuntimeError("Invalid api_addr URL.");
            }
            parsed.host = std::string(authority.substr(0, colon));
            parsed.port = parse_port_or_default(std::string(authority.substr(colon + 1)), 443);
        }
    }

    if (parsed.host.empty()) {
        throw RuntimeError("Invalid api_addr URL.");
    }

    return parsed;
}

ResolvedTarget resolve_target(const ParsedApiTarget& target) {
    auto resolved_addresses = resolve_numeric_addresses(target.host, target.port);
    return ResolvedTarget {
        .authority_target = target.authority(),
        .dial_target = format_target(resolved_addresses.front(), target.port),
        .resolved_addresses = std::move(resolved_addresses),
    };
}

std::shared_ptr<grpc::Channel> open_secure_channel(const ParsedApiTarget& target) {
    const auto resolved = resolve_target(target);
    return open_secure_channel(target, resolved);
}

std::shared_ptr<grpc::Channel> open_secure_channel(
    const ParsedApiTarget& target,
    const ResolvedTarget& resolved
) {
    grpc::ChannelArguments args;
    args.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
    args.SetString(GRPC_ARG_DEFAULT_AUTHORITY, target.authority());
    args.SetSslTargetNameOverride(target.host);
    return grpc::CreateCustomChannel(resolved.dial_target, grpc::SslCredentials({}), args);
}

std::shared_ptr<grpc::Channel> open_insecure_channel(const std::string& target) {
    grpc::ChannelArguments args;
    args.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
    return grpc::CreateCustomChannel(target, grpc::InsecureChannelCredentials(), args);
}

}  // namespace hackarena3::detail
